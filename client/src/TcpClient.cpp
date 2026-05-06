#include "TcpClient.hpp"
#include <QJsonDocument>

namespace { constexpr quint32 kMaxFrameSize = 1024 * 1024; }

TcpClient::TcpClient(QObject* parent) : QObject(parent) {
    m_socket = new QTcpSocket(this);
}

TcpClient::~TcpClient() {
    disconnectFromServer();
}

bool TcpClient::connectToServer(const QString& host, int port) {
    m_socket->connectToHost(host, static_cast<quint16>(port));
    return m_socket->waitForConnected(5000);
}

void TcpClient::disconnectFromServer() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(2000);
    }
    m_buffer.clear();
}

bool TcpClient::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QJsonObject TcpClient::sendRequest(const QJsonObject& request) {
    if (!isConnected())
        return {{"status", "error"}, {"message", "Not connected"}};

    // Re-entry guard: waitForReadyRead() may dispatch Qt events (e.g. QTimer
    // ticks calling refreshQuotes), and a nested sendRequest would steal the
    // bytes belonging to the outer request. Reject nested calls instead.
    if (m_inRequest)
        return {{"status", "error"}, {"message", "Request already in flight"}};
    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(m_inRequest);

    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    auto len = static_cast<quint32>(payload.size());

    // 4-byte big-endian length prefix (matches server framing)
    QByteArray frame(4 + payload.size(), '\0');
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >>  8) & 0xFF);
    frame[3] = static_cast<char>( len        & 0xFF);
    frame.replace(4, payload.size(), payload);

    if (m_socket->write(frame) < 0)
        return {{"status", "error"}, {"message", "Write failed"}};
    m_socket->flush();

    // Read until we have the 4-byte header
    while (m_buffer.size() < 4) {
        if (!m_socket->waitForReadyRead(5000))
            return {{"status", "error"}, {"message", "Timeout reading header"}};
        m_buffer += m_socket->readAll();
    }

    // Cast to quint32 BEFORE shifting to avoid signed-int overflow UB on byte 0x80+
    quint32 respLen = (static_cast<quint32>(static_cast<quint8>(m_buffer[0])) << 24)
                    | (static_cast<quint32>(static_cast<quint8>(m_buffer[1])) << 16)
                    | (static_cast<quint32>(static_cast<quint8>(m_buffer[2])) <<  8)
                    |  static_cast<quint32>(static_cast<quint8>(m_buffer[3]));

    if (respLen > kMaxFrameSize) {
        m_buffer.clear();
        m_socket->disconnectFromHost();
        return {{"status", "error"}, {"message", "Frame too large"}};
    }

    // Read until we have the full body
    while (m_buffer.size() < 4 + static_cast<int>(respLen)) {
        if (!m_socket->waitForReadyRead(5000))
            return {{"status", "error"}, {"message", "Timeout reading body"}};
        m_buffer += m_socket->readAll();
    }

    QByteArray body = m_buffer.mid(4, static_cast<int>(respLen));
    m_buffer.remove(0, 4 + static_cast<int>(respLen));

    auto doc = QJsonDocument::fromJson(body);
    if (doc.isNull())
        return {{"status", "error"}, {"message", "Invalid JSON from server"}};
    return doc.object();
}
