#include "NetworkWorker.hpp"

namespace { constexpr quint32 kMaxFrameSize = 1024 * 1024; }

NetworkWorker::NetworkWorker(QObject* parent) : QObject(parent) {}

NetworkWorker::~NetworkWorker() {
    resetSocket();
}

void NetworkWorker::resetSocket() {
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_buffer.clear();
    m_pending.clear();
}

void NetworkWorker::doConnect(const QString& host, int port) {
    resetSocket();
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &NetworkWorker::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &NetworkWorker::onSocketDisconnected);

    m_socket->connectToHost(host, static_cast<quint16>(port));
    if (m_socket->waitForConnected(5000)) {
        emit connectFinished(true, QString());
    } else {
        QString err = m_socket->errorString();
        resetSocket();
        emit connectFinished(false, err);
    }
}

void NetworkWorker::doDisconnect() {
    if (!m_socket) return;
    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->waitForDisconnected(1000);
    }
    resetSocket();
}

void NetworkWorker::doSendRequest(quint64 id, const QByteArray& payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        emit networkError(id, "Not connected");
        return;
    }

    auto len = static_cast<quint32>(payload.size());
    QByteArray frame(4 + payload.size(), '\0');
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >>  8) & 0xFF);
    frame[3] = static_cast<char>( len        & 0xFF);
    frame.replace(4, payload.size(), payload);

    if (m_socket->write(frame) < 0) {
        emit networkError(id, "Write failed");
        return;
    }
    m_pending.enqueue(id);
    m_socket->flush();
}

void NetworkWorker::onSocketReadyRead() {
    if (!m_socket) return;
    m_buffer += m_socket->readAll();
    parseResponses();
}

void NetworkWorker::parseResponses() {
    while (m_buffer.size() >= 4) {
        quint32 len = (static_cast<quint32>(static_cast<quint8>(m_buffer[0])) << 24)
                    | (static_cast<quint32>(static_cast<quint8>(m_buffer[1])) << 16)
                    | (static_cast<quint32>(static_cast<quint8>(m_buffer[2])) <<  8)
                    |  static_cast<quint32>(static_cast<quint8>(m_buffer[3]));

        if (len > kMaxFrameSize) {
            // Drain pending callers with an error and disconnect.
            while (!m_pending.isEmpty()) {
                emit networkError(m_pending.dequeue(), "Frame too large");
            }
            doDisconnect();
            return;
        }

        if (m_buffer.size() < 4 + static_cast<int>(len)) return;

        QByteArray body = m_buffer.mid(4, static_cast<int>(len));
        m_buffer.remove(0, 4 + static_cast<int>(len));

        if (m_pending.isEmpty()) {
            // Unsolicited frame — protocol is strict req/resp, ignore.
            continue;
        }
        emit responseReceived(m_pending.dequeue(), body);
    }
}

void NetworkWorker::onSocketDisconnected() {
    while (!m_pending.isEmpty()) {
        emit networkError(m_pending.dequeue(), "Disconnected");
    }
    m_buffer.clear();
    emit disconnected();
}
