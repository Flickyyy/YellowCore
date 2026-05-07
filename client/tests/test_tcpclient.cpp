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
    enum Mode {
        Echo,           // 1 request → echo it
        ByteByByte,     // 1 request → reply byte-by-byte
        MultiRequest,   // 2 requests → both echoed
        FrameTooLarge,  // 1 request → reply claiming length 2 GiB
        BadJson,        // 1 request → reply with malformed JSON
        DisconnectMid,  // 1 request → drop the connection without replying
        StressN         // N requests → reply with sequential id
    };

    MockServer(quint16 port, Mode mode, int n = 0)
        : m_port(port), m_mode(mode), m_n(n) {}

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
        } else if (m_mode == FrameTooLarge) {
            readOneRequest();
            // Announce a 2 GiB frame that we'll never deliver — client should
            // notice that len > kMaxFrameSize and tear down the connection.
            QByteArray hdr(4, '\0');
            quint32 huge = 0x7FFFFFFFu;
            hdr[0] = static_cast<char>((huge >> 24) & 0xFF);
            hdr[1] = static_cast<char>((huge >> 16) & 0xFF);
            hdr[2] = static_cast<char>((huge >>  8) & 0xFF);
            hdr[3] = static_cast<char>( huge        & 0xFF);
            sock->write(hdr);
            sock->flush();
            sock->waitForBytesWritten(500);
        } else if (m_mode == BadJson) {
            readOneRequest();
            QByteArray junk = "{ not valid json @@@";
            sock->write(frame(junk));
            sock->flush();
            sock->waitForBytesWritten(500);
        } else if (m_mode == DisconnectMid) {
            readOneRequest();
            // Hang up before sending any reply.
        } else if (m_mode == StressN) {
            for (int i = 0; i < m_n; ++i) {
                auto req = readOneRequest();
                writeReply({{"status", "ok"},
                            {"seq", i},
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
    int     m_n = 0;
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

    // ---- connection lifecycle: connect → isConnected → disconnect ----

    void connectionLifecycle() {
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::Echo);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(!client.isConnected());
        QVERIFY(client.connectToServer("127.0.0.1", port));
        QVERIFY(client.isConnected());
        client.disconnectFromServer();
        QVERIFY(!client.isConnected());

        server.wait(3000);
    }

    // ---- reconnect after disconnect ----

    void reconnectAfterDisconnect() {
        TcpClient client;

        // First session
        const quint16 port1 = pickFreePort();
        MockServer s1(port1, MockServer::Echo);
        s1.start();
        QTest::qWait(50);
        QVERIFY(client.connectToServer("127.0.0.1", port1));
        auto r1 = client.sendRequest({{"type", "first"}});
        QCOMPARE(r1["status"].toString(), QString("ok"));
        client.disconnectFromServer();
        s1.wait(3000);

        // Second session, fresh socket inside the same TcpClient
        const quint16 port2 = pickFreePort();
        MockServer s2(port2, MockServer::Echo);
        s2.start();
        QTest::qWait(50);
        QVERIFY(client.connectToServer("127.0.0.1", port2));
        auto r2 = client.sendRequest({{"type", "second"}});
        QCOMPARE(r2["status"].toString(), QString("ok"));
        QCOMPARE(r2["echo"].toObject()["type"].toString(), QString("second"));
        s2.wait(3000);
    }

    // ---- frame-too-large protection ----

    void frameTooLargeIsRejected() {
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::FrameTooLarge);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));
        auto resp = client.sendRequest({{"type", "ping"}});
        QCOMPARE(resp["status"].toString(), QString("error"));
        QVERIFY(resp["message"].toString().contains("too large", Qt::CaseInsensitive));

        server.wait(3000);
    }

    // ---- malformed JSON from server ----

    void malformedJsonReturnsError() {
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::BadJson);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));
        auto resp = client.sendRequest({{"type", "ping"}});
        QCOMPARE(resp["status"].toString(), QString("error"));
        QVERIFY(resp["message"].toString().contains("JSON", Qt::CaseInsensitive));

        server.wait(3000);
    }

    // ---- server hangs up mid-request ----

    void serverDisconnectsMidRequest() {
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::DisconnectMid);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));
        auto resp = client.sendRequest({{"type", "ping"}});
        QCOMPARE(resp["status"].toString(), QString("error"));
        // Could be "Disconnected" or "Timeout" depending on timing — both fine.
        QVERIFY(!resp["message"].toString().isEmpty());

        server.wait(3000);
    }

    // ---- stress: many sequential requests on one connection ----

    void stressManyRequests() {
        constexpr int N = 40;
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::StressN, N);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));
        for (int i = 0; i < N; ++i) {
            auto resp = client.sendRequest({{"type", QString("rq_%1").arg(i)}});
            QCOMPARE(resp["status"].toString(), QString("ok"));
            QCOMPARE(resp["seq"].toInt(), i);
        }
        server.wait(5000);
    }

    // ---- worker thread really runs off the UI thread ----

    void workerRunsOffMainThread() {
        // sendRequest must complete even though the test (= "UI") thread is
        // blocked inside QEventLoop::exec — this is only possible if the
        // socket I/O happens on a different QThread.
        const quint16 port = pickFreePort();
        MockServer server(port, MockServer::Echo);
        server.start();
        QTest::qWait(50);

        TcpClient client;
        QVERIFY(client.connectToServer("127.0.0.1", port));
        auto resp = client.sendRequest({{"type", "thread_check"}});
        QCOMPARE(resp["status"].toString(), QString("ok"));
        QCOMPARE(resp["echo"].toObject()["type"].toString(), QString("thread_check"));
        server.wait(3000);
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

    // ---- end-to-end full banking flow against the real server ----

    void e2eFullBankingFlow() {
        if (qgetenv("YELLOWCORE_E2E") != "1") {
            QSKIP("Set YELLOWCORE_E2E=1 with server running on :9090");
        }
        TcpClient c;
        QVERIFY(c.connectToServer("127.0.0.1", 9090));

        const QString user = QString("flow_%1").arg(QDateTime::currentMSecsSinceEpoch());
        QVERIFY(c.sendRequest({{"type","register"},{"username",user},{"password","x"}})
                  ["status"].toString() == "ok");
        auto login = c.sendRequest({{"type","login"},{"username",user},{"password","x"}});
        QString tok = login["token"].toString();
        QVERIFY(!tok.isEmpty());

        // Create USD account
        auto cr = c.sendRequest({{"type","create_account"},{"token",tok},{"currency","USD"}});
        QCOMPARE(cr["status"].toString(), QString("ok"));
        qint64 aid = cr["account_id"].toInteger();
        QVERIFY(aid > 0);

        // Deposit $1000
        auto dep = c.sendRequest({{"type","deposit"},{"token",tok},
                                  {"account_id",aid},{"amount",1000.0}});
        QCOMPARE(dep["status"].toString(), QString("ok"));
        QCOMPARE(dep["new_balance"].toDouble(), 1000.0);

        // Quotes available
        auto q = c.sendRequest({{"type","get_quotes"},{"token",tok}});
        QCOMPARE(q["status"].toString(), QString("ok"));
        QVERIFY(q["quotes"].toArray().size() == 8);

        // Exchange rates available
        auto fx = c.sendRequest({{"type","get_exchange_rates"},{"token",tok}});
        QCOMPARE(fx["status"].toString(), QString("ok"));
        QVERIFY(fx["rates"].toObject().size() >= 6);

        // History of one deposit
        auto hist = c.sendRequest({{"type","get_history"},{"token",tok},{"account_id",aid}});
        QCOMPARE(hist["status"].toString(), QString("ok"));
        QCOMPARE(hist["history"].toArray().size(), 1);

        // Logout invalidates token
        QCOMPARE(c.sendRequest({{"type","logout"},{"token",tok}})["status"].toString(),
                 QString("ok"));
        auto post = c.sendRequest({{"type","get_accounts"},{"token",tok}});
        QCOMPARE(post["status"].toString(), QString("error"));
    }
};

QTEST_MAIN(TestTcpClient)
#include "test_tcpclient.moc"
