#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>

class TcpClient : public QObject {
    Q_OBJECT
public:
    explicit TcpClient(QObject* parent = nullptr);
    ~TcpClient() override;

    bool connectToServer(const QString& host, int port);
    void disconnectFromServer();
    bool isConnected() const;

    // Blocking send/receive — fine for localhost
    QJsonObject sendRequest(const QJsonObject& request);

private:
    QTcpSocket* m_socket;
    QByteArray  m_buffer;
    bool        m_inRequest = false;  // re-entry guard: waitForReadyRead can dispatch events
};
