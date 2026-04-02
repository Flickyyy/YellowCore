#include <gtest/gtest.h>

#include "auth_service.hpp"
#include "bank_service.hpp"
#include "command_dispatcher.hpp"
#include "price_engine.hpp"
#include "stock_service.hpp"
#include "tcp_framing.hpp"
#include "tcp_server.hpp"

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

using boost::asio::ip::tcp;

void write_frame(tcp::socket& socket, const std::string& payload) {
    auto framed = frame_json_payload(payload);
    boost::asio::write(socket, boost::asio::buffer(framed));
}

nlohmann::json read_frame_json(tcp::socket& socket) {
    std::array<std::uint8_t, 4> header{};
    boost::asio::read(socket, boost::asio::buffer(header));

    auto length = decode_be_u32(header.data());
    if (length == 0 || length > kMaxFrameSize) {
        throw std::runtime_error("Invalid frame length in response");
    }

    std::string body(length, '\0');
    boost::asio::read(socket, boost::asio::buffer(body));
    return nlohmann::json::parse(body);
}

std::unordered_map<std::string, double> to_quote_map(const nlohmann::json& quotes_array) {
    std::unordered_map<std::string, double> out;
    for (const auto& item : quotes_array) {
        out[item.at("ticker").get<std::string>()] = item.at("price").get<double>();
    }
    return out;
}

bool wait_for_server(unsigned short port, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        boost::asio::io_context io;
        tcp::socket socket(io);
        boost::system::error_code ec;
        [[maybe_unused]] const auto endpoint =
            socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port), ec);
        if (!ec) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return false;
}

class TestClient {
public:
    void connect(unsigned short port) {
        socket_.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));
    }

    nlohmann::json request(const nlohmann::json& request_json) {
        write_frame(socket_, request_json.dump());
        return read_frame_json(socket_);
    }

    nlohmann::json request_raw_payload(const std::string& payload) {
        write_frame(socket_, payload);
        return read_frame_json(socket_);
    }

    tcp::socket& socket() { return socket_; }

private:
    boost::asio::io_context io_;
    tcp::socket socket_{io_};
};

class NetworkFixture : public ::testing::Test {
protected:
    void SetUp() override {
        prices_.start();

        server_ = std::make_unique<TcpServer>(
            boost::asio::ip::make_address("127.0.0.1"),
            0,
            2,
            dispatcher_);
        port_ = server_->port();

        server_thread_ = std::thread([this] { server_->run(); });
        ASSERT_TRUE(wait_for_server(port_, std::chrono::seconds(2)));
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        prices_.stop();
    }

    unsigned short port() const { return port_; }

private:
    AuthService auth_;
    BankService bank_;
    PriceEngine prices_;
    StockService stock_{bank_, prices_};
    CommandDispatcher dispatcher_{auth_, bank_, stock_, prices_};

    std::unique_ptr<TcpServer> server_;
    std::thread server_thread_;
    unsigned short port_ = 0;
};

TEST_F(NetworkFixture, AuthAndAccountHappyPath) {
    TestClient client;
    client.connect(port());

    auto reg = client.request({
        {"type", "register"},
        {"username", "net_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(reg.value("status", ""), "ok");
    ASSERT_TRUE(reg.contains("user_id"));

    auto login = client.request({
        {"type", "login"},
        {"username", "net_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(login.value("status", ""), "ok");
    ASSERT_TRUE(login.contains("token"));
    std::string token = login["token"].get<std::string>();

    auto create = client.request({
        {"type", "create_account"},
        {"token", token},
        {"currency", "RUB"}
    });
    ASSERT_EQ(create.value("status", ""), "ok");
    uint64_t account_id = create["account_id"].get<uint64_t>();

    auto dep = client.request({
        {"type", "deposit"},
        {"token", token},
        {"account_id", account_id},
        {"amount", 1000.0}
    });
    ASSERT_EQ(dep.value("status", ""), "ok");
    ASSERT_DOUBLE_EQ(dep["new_balance"].get<double>(), 1000.0);

    auto accs = client.request({
        {"type", "get_accounts"},
        {"token", token}
    });
    ASSERT_EQ(accs.value("status", ""), "ok");
    ASSERT_TRUE(accs.contains("accounts"));
    ASSERT_TRUE(accs["accounts"].is_array());
    ASSERT_EQ(accs["accounts"].size(), 1u);
    ASSERT_EQ(accs["accounts"][0]["id"].get<uint64_t>(), account_id);
    ASSERT_EQ(accs["accounts"][0]["currency"].get<std::string>(), "RUB");
    ASSERT_DOUBLE_EQ(accs["accounts"][0]["balance"].get<double>(), 1000.0);
}

TEST_F(NetworkFixture, MoneyFlowHistoryRatesAndCloseAccount) {
    TestClient client;
    client.connect(port());

    auto reg = client.request({
        {"type", "register"},
        {"username", "flow_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(reg.value("status", ""), "ok");

    auto login = client.request({
        {"type", "login"},
        {"username", "flow_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(login.value("status", ""), "ok");
    const std::string token = login["token"].get<std::string>();

    auto create_rub = client.request({
        {"type", "create_account"},
        {"token", token},
        {"currency", "RUB"}
    });
    ASSERT_EQ(create_rub.value("status", ""), "ok");
    const uint64_t rub = create_rub["account_id"].get<uint64_t>();

    auto create_usd = client.request({
        {"type", "create_account"},
        {"token", token},
        {"currency", "USD"}
    });
    ASSERT_EQ(create_usd.value("status", ""), "ok");
    const uint64_t usd = create_usd["account_id"].get<uint64_t>();

    auto dep = client.request({
        {"type", "deposit"},
        {"token", token},
        {"account_id", rub},
        {"amount", 10000.0}
    });
    ASSERT_EQ(dep.value("status", ""), "ok");

    auto transfer = client.request({
        {"type", "transfer"},
        {"token", token},
        {"from_account", rub},
        {"to_account", usd},
        {"amount", 9250.0}
    });
    ASSERT_EQ(transfer.value("status", ""), "ok");
    ASSERT_GT(transfer.at("converted_amount").get<double>(), 0.0);

    auto withdraw = client.request({
        {"type", "withdraw"},
        {"token", token},
        {"account_id", rub},
        {"amount", 200.0}
    });
    ASSERT_EQ(withdraw.value("status", ""), "ok");

    auto history = client.request({
        {"type", "get_history"},
        {"token", token},
        {"account_id", rub}
    });
    ASSERT_EQ(history.value("status", ""), "ok");
    ASSERT_TRUE(history["history"].is_array());
    ASSERT_GE(history["history"].size(), 3u);

    auto filtered = client.request({
        {"type", "get_history"},
        {"token", token},
        {"account_id", rub},
        {"filter_type", "withdraw"}
    });
    ASSERT_EQ(filtered.value("status", ""), "ok");
    ASSERT_TRUE(filtered["history"].is_array());
    ASSERT_EQ(filtered["history"].size(), 1u);
    ASSERT_EQ(filtered["history"][0]["op_type"].get<std::string>(), "withdraw");

    auto from_future = client.request({
        {"type", "get_history"},
        {"token", token},
        {"account_id", rub},
        {"from_date", 4102444800LL}
    });
    ASSERT_EQ(from_future.value("status", ""), "ok");
    ASSERT_TRUE(from_future["history"].is_array());
    ASSERT_EQ(from_future["history"].size(), 0u);

    auto invalid_range = client.request({
        {"type", "get_history"},
        {"token", token},
        {"account_id", rub},
        {"from_date", 200LL},
        {"to_date", 100LL}
    });
    ASSERT_EQ(invalid_range.value("status", ""), "error");
    ASSERT_EQ(invalid_range.value("message", ""), "Invalid date range");

    auto rates = client.request({
        {"type", "get_exchange_rates"},
        {"token", token}
    });
    ASSERT_EQ(rates.value("status", ""), "ok");
    ASSERT_TRUE(rates.contains("rates"));
    ASSERT_TRUE(rates["rates"].contains("USD_RUB"));

    auto close_fail = client.request({
        {"type", "close_account"},
        {"token", token},
        {"account_id", rub}
    });
    ASSERT_EQ(close_fail.value("status", ""), "error");

    auto withdraw_all = client.request({
        {"type", "withdraw"},
        {"token", token},
        {"account_id", rub},
        {"amount", 550.0}
    });
    ASSERT_EQ(withdraw_all.value("status", ""), "ok");

    auto close_ok = client.request({
        {"type", "close_account"},
        {"token", token},
        {"account_id", rub}
    });
    ASSERT_EQ(close_ok.value("status", ""), "ok");
}

TEST_F(NetworkFixture, InvalidTokenRejected) {
    TestClient client;
    client.connect(port());

    auto response = client.request({
        {"type", "get_accounts"},
        {"token", "bogus"}
    });

    ASSERT_EQ(response.value("status", ""), "error");
    ASSERT_EQ(response.value("message", ""), "Invalid token");
}

TEST_F(NetworkFixture, LoginWithInvalidCredentialsReturnsError) {
    TestClient client;
    client.connect(port());

    auto reg = client.request({
        {"type", "register"},
        {"username", "auth_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(reg.value("status", ""), "ok");

    auto bad_login = client.request({
        {"type", "login"},
        {"username", "auth_alice"},
        {"password", "wrong"}
    });
    ASSERT_EQ(bad_login.value("status", ""), "error");
    ASSERT_EQ(bad_login.value("message", ""), "Invalid credentials");
}

TEST_F(NetworkFixture, LogoutInvalidatesToken) {
    TestClient client;
    client.connect(port());

    auto reg = client.request({
        {"type", "register"},
        {"username", "logout_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(reg.value("status", ""), "ok");

    auto login = client.request({
        {"type", "login"},
        {"username", "logout_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(login.value("status", ""), "ok");
    const std::string token = login["token"].get<std::string>();

    auto logout = client.request({
        {"type", "logout"},
        {"token", token}
    });
    ASSERT_EQ(logout.value("status", ""), "ok");

    auto after_logout = client.request({
        {"type", "get_accounts"},
        {"token", token}
    });
    ASSERT_EQ(after_logout.value("status", ""), "error");
    ASSERT_EQ(after_logout.value("message", ""), "Invalid token");
}

TEST_F(NetworkFixture, BadJsonPayloadReturnsError) {
    TestClient client;
    client.connect(port());

    auto response = client.request_raw_payload("{bad_json");

    ASSERT_EQ(response.value("status", ""), "error");
    ASSERT_TRUE(response.contains("message"));
    ASSERT_NE(response["message"].get<std::string>().find("Bad JSON"), std::string::npos);
}

TEST_F(NetworkFixture, ZeroFrameLengthReturnsErrorAndClosesSocket) {
    TestClient client;
    client.connect(port());

    std::array<std::uint8_t, 4> zero_header{0, 0, 0, 0};
    boost::asio::write(client.socket(), boost::asio::buffer(zero_header));

    auto response = read_frame_json(client.socket());
    ASSERT_EQ(response.value("status", ""), "error");
    ASSERT_EQ(response.value("message", ""), "Invalid frame size");

    std::array<char, 1> probe{};
    boost::system::error_code ec;
    client.socket().read_some(boost::asio::buffer(probe), ec);
    ASSERT_TRUE(ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset);
}

TEST_F(NetworkFixture, StockTradeHappyPathOverTcp) {
    TestClient client;
    client.connect(port());

    auto reg = client.request({
        {"type", "register"},
        {"username", "stock_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(reg.value("status", ""), "ok");

    auto login = client.request({
        {"type", "login"},
        {"username", "stock_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(login.value("status", ""), "ok");
    std::string token = login["token"].get<std::string>();

    auto create = client.request({
        {"type", "create_account"},
        {"token", token},
        {"currency", "USD"}
    });
    ASSERT_EQ(create.value("status", ""), "ok");
    uint64_t account_id = create["account_id"].get<uint64_t>();

    auto dep = client.request({
        {"type", "deposit"},
        {"token", token},
        {"account_id", account_id},
        {"amount", 10000.0}
    });
    ASSERT_EQ(dep.value("status", ""), "ok");

    auto buy = client.request({
        {"type", "buy_stock"},
        {"token", token},
        {"ticker", "AAPL"},
        {"quantity", 2},
        {"account_id", account_id}
    });
    ASSERT_EQ(buy.value("status", ""), "ok");
    ASSERT_GT(buy.at("price").get<double>(), 0.0);
    ASSERT_GT(buy.at("total_cost").get<double>(), 0.0);

    auto portfolio = client.request({
        {"type", "get_portfolio"},
        {"token", token}
    });
    ASSERT_EQ(portfolio.value("status", ""), "ok");
    ASSERT_TRUE(portfolio["positions"].is_array());
    ASSERT_EQ(portfolio["positions"].size(), 1u);
    ASSERT_EQ(portfolio["positions"][0]["ticker"].get<std::string>(), "AAPL");
    ASSERT_EQ(portfolio["positions"][0]["quantity"].get<int>(), 2);
    ASSERT_TRUE(portfolio["positions"][0].contains("current_price"));
    ASSERT_TRUE(portfolio["positions"][0].contains("pnl"));

    auto sell = client.request({
        {"type", "sell_stock"},
        {"token", token},
        {"ticker", "AAPL"},
        {"quantity", 1},
        {"account_id", account_id}
    });
    ASSERT_EQ(sell.value("status", ""), "ok");
    ASSERT_GT(sell.at("total_revenue").get<double>(), 0.0);

    auto trades = client.request({
        {"type", "get_trades"},
        {"token", token}
    });
    ASSERT_EQ(trades.value("status", ""), "ok");
    ASSERT_TRUE(trades["trades"].is_array());
    ASSERT_EQ(trades["trades"].size(), 2u);
    ASSERT_EQ(trades["trades"][0]["side"].get<std::string>(), "buy");
    ASSERT_EQ(trades["trades"][1]["side"].get<std::string>(), "sell");
}

TEST_F(NetworkFixture, CloseAccountBlockedByOpenStockPositions) {
    TestClient client;
    client.connect(port());

    auto reg = client.request({
        {"type", "register"},
        {"username", "close_stock_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(reg.value("status", ""), "ok");

    auto login = client.request({
        {"type", "login"},
        {"username", "close_stock_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(login.value("status", ""), "ok");
    const std::string token = login["token"].get<std::string>();

    auto create = client.request({
        {"type", "create_account"},
        {"token", token},
        {"currency", "USD"}
    });
    ASSERT_EQ(create.value("status", ""), "ok");
    const uint64_t account_id = create["account_id"].get<uint64_t>();

    auto dep = client.request({
        {"type", "deposit"},
        {"token", token},
        {"account_id", account_id},
        {"amount", 10000.0}
    });
    ASSERT_EQ(dep.value("status", ""), "ok");

    auto buy = client.request({
        {"type", "buy_stock"},
        {"token", token},
        {"ticker", "AAPL"},
        {"quantity", 1},
        {"account_id", account_id}
    });
    ASSERT_EQ(buy.value("status", ""), "ok");

    const double remainder = buy["new_balance"].get<double>();
    auto withdraw = client.request({
        {"type", "withdraw"},
        {"token", token},
        {"account_id", account_id},
        {"amount", remainder}
    });
    ASSERT_EQ(withdraw.value("status", ""), "ok");

    auto close_fail = client.request({
        {"type", "close_account"},
        {"token", token},
        {"account_id", account_id}
    });
    ASSERT_EQ(close_fail.value("status", ""), "error");
    ASSERT_EQ(close_fail.value("message", ""), "Account has open stock positions");

    auto sell = client.request({
        {"type", "sell_stock"},
        {"token", token},
        {"ticker", "AAPL"},
        {"quantity", 1},
        {"account_id", account_id}
    });
    ASSERT_EQ(sell.value("status", ""), "ok");

    const double after_sell = sell["new_balance"].get<double>();
    auto withdraw_after_sell = client.request({
        {"type", "withdraw"},
        {"token", token},
        {"account_id", account_id},
        {"amount", after_sell}
    });
    ASSERT_EQ(withdraw_after_sell.value("status", ""), "ok");

    auto close_ok = client.request({
        {"type", "close_account"},
        {"token", token},
        {"account_id", account_id}
    });
    ASSERT_EQ(close_ok.value("status", ""), "ok");
}

TEST_F(NetworkFixture, PriceEngineChangesQuotesOverTime) {
    TestClient client;
    client.connect(port());

    auto reg = client.request({
        {"type", "register"},
        {"username", "quotes_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(reg.value("status", ""), "ok");

    auto login = client.request({
        {"type", "login"},
        {"username", "quotes_alice"},
        {"password", "pass123"}
    });
    ASSERT_EQ(login.value("status", ""), "ok");
    std::string token = login["token"].get<std::string>();

    auto q1 = client.request({
        {"type", "get_quotes"},
        {"token", token}
    });
    ASSERT_EQ(q1.value("status", ""), "ok");
    auto baseline = to_quote_map(q1.at("quotes"));
    ASSERT_FALSE(baseline.empty());

    bool changed = false;
    for (int attempt = 0; attempt < 10 && !changed; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        auto qn = client.request({
            {"type", "get_quotes"},
            {"token", token}
        });
        ASSERT_EQ(qn.value("status", ""), "ok");
        auto current = to_quote_map(qn.at("quotes"));

        for (const auto& [ticker, price] : baseline) {
            auto it = current.find(ticker);
            ASSERT_TRUE(it != current.end());
            if (price != it->second) {
                changed = true;
                break;
            }
        }
    }

    ASSERT_TRUE(changed);
}

TEST_F(NetworkFixture, UnknownCommandReturnsError) {
    TestClient client;
    client.connect(port());

    auto response = client.request({
        {"type", "definitely_unknown_command"}
    });

    ASSERT_EQ(response.value("status", ""), "error");
    ASSERT_EQ(response.value("message", ""), "Unknown command type");
}

}  // namespace
