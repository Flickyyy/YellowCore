#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QThread>
#include <QDateTime>
#include "TcpClient.hpp"

namespace {

QByteArray frame(const QByteArray& payload) {
    auto len = static_cast<quint32>(payload.size());
    QByteArray header(4, '\0');
    header[0] = static_cast<char>((len >> 24) & 0xFF);
    header[1] = static_cast<char>((len >> 16) & 0xFF);
    header[2] = static_cast<char>((len >>  8) & 0xFF);
    header[3] = static_cast<char>( len        & 0xFF);
    return header + payload;
}

quint32 decodeLen(const QByteArray& header4) {
    return (static_cast<quint8>(header4[0]) << 24)
         | (static_cast<quint8>(header4[1]) << 16)
         | (static_cast<quint8>(header4[2]) <<  8)
         |  static_cast<quint8>(header4[3]);
}

// Synchronous mock-server thread — runs entirely inside `run()` and exits
// naturally so we don't need an event loop or quit() signalling.
class MockServer : public QThread {
public:
    enum Mode { Echo, ByteByByte, MultiRequest };

    MockServer(quint16 port, Mode mode) : m_port(port), m_mode(mode) {}

protected:
    void run() override {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, m_port)) return;
        if (!server.waitForNewConnection(5000)) return;

        QTcpSocket* sock = server.nextPendingConnection();
        if (!sock) return;

        auto readOneRequest = [sock]() -> QJsonObject {
            QByteArray buf;
            while (buf.size() < 4) {
                if (!sock->waitForReadyRead(2000)) return {};
                buf += sock->readAll();
            }
            quint32 len = decodeLen(buf.left(4));
            while (buf.size() < 4 + static_cast<int>(len)) {
                if (!sock->waitForReadyRead(2000)) return {};
                buf += sock->readAll();
            }
            return QJsonDocument::fromJson(buf.mid(4, len)).object();
        };

        auto writeReply = [sock](const QJsonObject& resp) {
            sock->write(frame(QJsonDocument(resp).toJson(QJsonDocument::Compact)));
            sock->flush();
            sock->waitForBytesWritten(1000);
        };

        if (m_mode == Echo) {
            auto req = readOneRequest();
            writeReply({{"status", "ok"}, {"echo", req}});
        } else if (m_mode == ByteByByte) {
            readOneRequest();
            QByteArray framed = frame(QJsonDocument(
                QJsonObject{{"status", "ok"}, {"value", 42}})
                    .toJson(QJsonDocument::Compact));
            for (char c : framed) {
                sock->write(QByteArray(1, c));
                sock->flush();
                sock->waitForBytesWritten(200);
                QThread::msleep(2);
            }
        } else if (m_mode == MultiRequest) {
            for (int i = 0; i < 2; ++i) {
                auto req = readOneRequest();
                writeReply({{"status", "ok"},
                            {"got", req["type"].toString()}});
            }
        }

        sock->disconnectFromHost();
        if (sock->state() != QAbstractSocket::UnconnectedState)
            sock->waitForDisconnected(1000);
    }

private:
    quint16 m_port;
    Mode    m_mode;
};

quint16 pickFreePort() {
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();  // probe is closed when destructed
}

}  // namespace

class TestTcpClient : public QObject {
    Q_OBJECT

private slots:

    // ---- offline / error-path tests ----

    void connectFailReturnsError() {
        TcpClient client;
        bool ok = client.connectToServer("127.0.0.1", 1);
        QVERIFY(!ok);
        QVERIFY(!client.isConnected());
    }

    void requestWhileDisconnectedReturnsError() {
        TcpClient client;
        auto resp = client.sendRequest({{"type", "ping"}});
        QCOMPARE(resp["status"].toString(), QString("error"));
        QVERIFY(!resp["message"].toString().isEmpty());
    }

    // ---- framing round-trip against a mock server in another thread ----

    void framingRoundTripWithEcho() {
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::Echo);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));

        QJsonObject req{{"type", "hello"}, {"payload", "world"}};
        auto resp = client.sendRequest(req);
        QCOMPARE(resp["status"].toString(), QString("ok"));
        auto echoed = resp["echo"].toObject();
        QCOMPARE(echoed["type"].toString(),    QString("hello"));
        QCOMPARE(echoed["payload"].toString(), QString("world"));

        QVERIFY(server.wait(3000));
    }

    // ---- partial-read robustness: server writes byte-by-byte ----

    void framingHandlesByteByByteResponse() {
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::ByteByByte);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));

        auto resp = client.sendRequest({{"type", "ping"}});
        QCOMPARE(resp["status"].toString(), QString("ok"));
        QCOMPARE(resp["value"].toInt(), 42);

        QVERIFY(server.wait(3000));
    }

    // ---- two requests on the same connection ----

    void multipleRequestsReuseConnection() {
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::MultiRequest);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));

        auto r1 = client.sendRequest({{"type", "first"}});
        auto r2 = client.sendRequest({{"type", "second"}});

        QCOMPARE(r1["got"].toString(), QString("first"));
        QCOMPARE(r2["got"].toString(), QString("second"));

        QVERIFY(server.wait(3000));
    }

    // ---- end-to-end against the real yellowcore_server, if it's running ----
    // Skipped unless YELLOWCORE_E2E=1 is set, to keep ctest hermetic.

    void e2eRegisterLoginGetAccounts() {
        if (qgetenv("YELLOWCORE_E2E") != "1") {
            QSKIP("Set YELLOWCORE_E2E=1 with server running on :9090");
        }
        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", 9090));

        const QString user = QString("test_%1").arg(QDateTime::currentMSecsSinceEpoch());
        auto reg = client.sendRequest({
            {"type", "register"}, {"username", user}, {"password", "pw"}});
        QCOMPARE(reg["status"].toString(), QString("ok"));

        auto login = client.sendRequest({
            {"type", "login"}, {"username", user}, {"password", "pw"}});
        QCOMPARE(login["status"].toString(), QString("ok"));
        const QString token = login["token"].toString();
        QVERIFY(!token.isEmpty());

        auto accs = client.sendRequest({{"type", "get_accounts"}, {"token", token}});
        QCOMPARE(accs["status"].toString(), QString("ok"));
    }
};

QTEST_MAIN(TestTcpClient)
#include "test_tcpclient.moc"
