#include "TcpClient.hpp"
#include "NetworkWorker.hpp"
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>

TcpClient::TcpClient(QObject* parent) : QObject(parent) {
    m_thread = new QThread(this);
    m_thread->setObjectName("YC-Network");
    m_worker = new NetworkWorker;
    m_worker->moveToThread(m_thread);

    // Auto-delete worker when thread finishes.
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // UI → worker (queued because they live in different threads).
    connect(this, &TcpClient::requestConnect,
            m_worker, &NetworkWorker::doConnect);
    connect(this, &TcpClient::requestDisconnect,
            m_worker, &NetworkWorker::doDisconnect);
    connect(this, &TcpClient::requestSend,
            m_worker, &NetworkWorker::doSendRequest);

    // Track passive disconnects (e.g., server closed the socket).
    connect(m_worker, &NetworkWorker::disconnected,
            this, &TcpClient::onWorkerDisconnected);

    m_thread->start();
}

TcpClient::~TcpClient() {
    disconnectFromServer();
    m_thread->quit();
    m_thread->wait(2000);
}

void TcpClient::onWorkerDisconnected() {
    m_connected = false;
}

bool TcpClient::connectToServer(const QString& host, int port) {
    if (m_connected) return true;

    QEventLoop loop;
    bool ok = false;

    auto conn = connect(m_worker, &NetworkWorker::connectFinished,
        this, [&](bool result, const QString& /*error*/) {
            ok = result;
            loop.quit();
        }, Qt::QueuedConnection);

    QTimer guard;
    guard.setSingleShot(true);
    connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(6000);

    emit requestConnect(host, port);
    loop.exec();

    disconnect(conn);
    m_connected = ok;
    return ok;
}

void TcpClient::disconnectFromServer() {
    if (!m_connected) return;
    emit requestDisconnect();
    m_connected = false;
}

QJsonObject TcpClient::sendRequest(const QJsonObject& request) {
    if (!m_connected)
        return {{"status", "error"}, {"message", "Not connected"}};

    // Re-entry guard: nested sendRequest (e.g., a QTimer slot firing while the
    // outer call is inside QEventLoop::exec) would interleave responses. The
    // worker is FIFO-correct internally, but the callers' QEventLoops would
    // race for the wrong frame. Reject the inner call instead.
    if (m_inRequest)
        return {{"status", "error"}, {"message", "Request already in flight"}};
    struct Guard {
        bool& f; explicit Guard(bool& x) : f(x) { f = true; } ~Guard() { f = false; }
    } g(m_inRequest);

    quint64 id = ++m_nextId;
    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);

    QEventLoop loop;
    QJsonObject result;
    bool gotResponse = false;
    QString errMsg;

    auto okConn = connect(m_worker, &NetworkWorker::responseReceived,
        this, [&](quint64 rid, const QByteArray& body) {
            if (rid != id) return;
            auto doc = QJsonDocument::fromJson(body);
            if (doc.isNull()) {
                errMsg = "Invalid JSON from server";
            } else {
                result = doc.object();
                gotResponse = true;
            }
            loop.quit();
        }, Qt::QueuedConnection);

    auto errConn = connect(m_worker, &NetworkWorker::networkError,
        this, [&](quint64 rid, const QString& err) {
            if (rid != id) return;
            errMsg = err;
            loop.quit();
        }, Qt::QueuedConnection);

    QTimer guard;
    guard.setSingleShot(true);
    connect(&guard, &QTimer::timeout, &loop, [&]() {
        errMsg = "Timeout";
        loop.quit();
    });
    guard.start(5000);

    emit requestSend(id, payload);
    loop.exec();

    disconnect(okConn);
    disconnect(errConn);

    if (gotResponse) return result;
    return {{"status", "error"},
            {"message", errMsg.isEmpty() ? "Unknown error" : errMsg}};
}
