#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QQueue>
#include <QString>

// NetworkWorker owns the QTcpSocket and lives in a worker QThread.
// All socket I/O happens off the UI thread; communication with TcpClient is
// done via Qt::QueuedConnection signals/slots.
class NetworkWorker : public QObject {
    Q_OBJECT
public:
    explicit NetworkWorker(QObject* parent = nullptr);
    ~NetworkWorker() override;

public slots:
    // Triggered from UI thread via queued connection.
    void doConnect(const QString& host, int port);
    void doDisconnect();
    void doSendRequest(quint64 id, const QByteArray& payload);

signals:
    // Emitted to UI thread via queued connection.
    void connectFinished(bool ok, const QString& error);
    void disconnected();
    void responseReceived(quint64 id, const QByteArray& body);
    void networkError(quint64 id, const QString& error);

private slots:
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    QTcpSocket* m_socket = nullptr;
    QByteArray  m_buffer;
    QQueue<quint64> m_pending;   // FIFO of in-flight request IDs

    void parseResponses();
    void resetSocket();
};
