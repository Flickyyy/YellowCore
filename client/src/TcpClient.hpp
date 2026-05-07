#pragma once
#include <QObject>
#include <QThread>
#include <QJsonObject>

class NetworkWorker;

// TcpClient is a thin facade in the UI thread over a NetworkWorker that runs
// in its own QThread. The public API is synchronous, but the actual socket
// I/O happens off the UI thread — sendRequest waits via a local QEventLoop
// while the worker performs the real work.
class TcpClient : public QObject {
    Q_OBJECT
public:
    explicit TcpClient(QObject* parent = nullptr);
    ~TcpClient() override;

    bool        connectToServer(const QString& host, int port);
    void        disconnectFromServer();
    bool        isConnected() const { return m_connected; }
    QJsonObject sendRequest(const QJsonObject& request);

signals:
    // Sent from UI thread → worker (via queued connection).
    void requestConnect(const QString& host, int port);
    void requestDisconnect();
    void requestSend(quint64 id, const QByteArray& payload);

private slots:
    void onWorkerDisconnected();

private:
    QThread*       m_thread;
    NetworkWorker* m_worker;
    bool           m_connected = false;
    bool           m_inRequest = false;   // re-entry guard
    quint64        m_nextId    = 0;
};
