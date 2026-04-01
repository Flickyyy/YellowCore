#include "command_dispatcher.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>

namespace {

template <typename T>
bool extract_required(const nlohmann::json& request, const char* key, T& out) {
    if (!request.contains(key)) {
        return false;
    }
    try {
        out = request.at(key).get<T>();
        return true;
    } catch (...) {
        return false;
    }
}

bool extract_optional_epoch_seconds(const nlohmann::json& request, const char* key,
                                    std::optional<int64_t>& out) {
    if (!request.contains(key)) {
        out.reset();
        return true;
    }

    const auto& value = request.at(key);
    try {
        if (value.is_number_integer() || value.is_number_unsigned()) {
            out = value.get<int64_t>();
            return true;
        }

        if (value.is_string()) {
            const std::string s = value.get<std::string>();
            std::size_t pos = 0;
            const long long parsed = std::stoll(s, &pos);
            if (pos != s.size()) {
                return false;
            }
            out = static_cast<int64_t>(parsed);
            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
}

}  // namespace

CommandDispatcher::CommandDispatcher(IAuthService& auth, IBankService& bank, IStockService& stock, PriceEngine& prices)
    : auth_(auth), bank_(bank), stock_(stock), prices_(prices) {}

nlohmann::json CommandDispatcher::handle_message(const nlohmann::json& request) const {
    std::string type;
    if (!extract_required(request, "type", type)) {
        return error_response("Missing field: type");
    }

    if (type == "register") return handle_register(request);
    if (type == "login") return handle_login(request);
    if (type == "logout") return handle_logout(request);
    if (type == "create_account") return handle_create_account(request);
    if (type == "get_accounts") return handle_get_accounts(request);
    if (type == "close_account") return handle_close_account(request);
    if (type == "deposit") return handle_deposit(request);
    if (type == "withdraw") return handle_withdraw(request);
    if (type == "transfer") return handle_transfer(request);
    if (type == "get_history") return handle_get_history(request);
    if (type == "get_quotes") return handle_get_quotes(request);
    if (type == "get_exchange_rates") return handle_get_exchange_rates(request);
    if (type == "buy_stock") return handle_buy_stock(request);
    if (type == "sell_stock") return handle_sell_stock(request);
    if (type == "get_portfolio") return handle_get_portfolio(request);
    if (type == "get_trades") return handle_get_trades(request);

    return error_response("Unknown command type");
}

nlohmann::json CommandDispatcher::handle_register(const nlohmann::json& request) const {
    std::string username;
    std::string password;
    if (!extract_required(request, "username", username) ||
        !extract_required(request, "password", password)) {
        return error_response("Missing field: username/password");
    }

    auto id = auth_.register_user(username, password);
    if (!id) return error_response("Username taken");

    return {
        {"status", "ok"},
        {"user_id", *id}
    };
}

nlohmann::json CommandDispatcher::handle_login(const nlohmann::json& request) const {
    std::string username;
    std::string password;
    if (!extract_required(request, "username", username) ||
        !extract_required(request, "password", password)) {
        return error_response("Missing field: username/password");
    }

    auto token = auth_.login(username, password);
    if (!token) return error_response("Invalid credentials");

    return {
        {"status", "ok"},
        {"token", *token}
    };
}

nlohmann::json CommandDispatcher::handle_logout(const nlohmann::json& request) const {
    std::string token;
    if (!extract_required(request, "token", token)) {
        return error_response("Missing field: token");
    }

    if (!auth_.logout(token)) {
        return error_response("Invalid token");
    }

    return {{"status", "ok"}};
}

nlohmann::json CommandDispatcher::handle_create_account(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    std::string currency_str;
    if (!extract_required(request, "currency", currency_str)) {
        return error_response("Missing field: currency");
    }
    auto currency = currency_from_string(currency_str);
    if (!currency) return error_response("Invalid currency");

    auto account_id = bank_.create_account(*user_id, *currency);
    return {
        {"status", "ok"},
        {"account_id", account_id}
    };
}

nlohmann::json CommandDispatcher::handle_get_accounts(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    auto accounts = bank_.get_accounts(*user_id);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& account : accounts) {
        arr.push_back({
            {"id", account.id},
            {"currency", to_string(account.currency)},
            {"balance", account.balance}
        });
    }

    return {
        {"status", "ok"},
        {"accounts", arr}
    };
}

nlohmann::json CommandDispatcher::handle_close_account(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    uint64_t account_id;
    if (!extract_required(request, "account_id", account_id)) {
        return error_response("Missing field: account_id");
    }

    if (stock_.has_open_positions_on_account(*user_id, account_id)) {
        return error_response("Account has open stock positions");
    }

    if (!bank_.close_account(*user_id, account_id)) {
        return error_response("Close account failed");
    }

    return {{"status", "ok"}};
}

nlohmann::json CommandDispatcher::handle_deposit(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    uint64_t account_id;
    double amount;
    if (!extract_required(request, "account_id", account_id) ||
        !extract_required(request, "amount", amount)) {
        return error_response("Missing field: account_id/amount");
    }

    auto new_balance = bank_.deposit(*user_id, account_id, amount);
    if (!new_balance) return error_response("Deposit failed");

    return {
        {"status", "ok"},
        {"new_balance", *new_balance}
    };
}

nlohmann::json CommandDispatcher::handle_withdraw(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    uint64_t account_id;
    double amount;
    if (!extract_required(request, "account_id", account_id) ||
        !extract_required(request, "amount", amount)) {
        return error_response("Missing field: account_id/amount");
    }

    auto new_balance = bank_.withdraw(*user_id, account_id, amount);
    if (!new_balance) return error_response("Withdraw failed");

    return {
        {"status", "ok"},
        {"new_balance", *new_balance}
    };
}

nlohmann::json CommandDispatcher::handle_transfer(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    uint64_t from_account;
    uint64_t to_account;
    double amount;
    if (!extract_required(request, "from_account", from_account) ||
        !extract_required(request, "to_account", to_account) ||
        !extract_required(request, "amount", amount)) {
        return error_response("Missing field: from_account/to_account/amount");
    }

    auto from = bank_.get_account(from_account);
    auto to = bank_.get_account(to_account);
    if (!from || !to) {
        return error_response("Account not found");
    }

    double rate = prices_.get_rate(from->currency, to->currency);
    auto transfer_result = bank_.transfer(*user_id, from_account, to_account, amount, rate);
    if (!transfer_result) {
        return error_response("Transfer failed");
    }

    return {
        {"status", "ok"},
        {"from_balance", transfer_result->from_balance},
        {"to_balance", transfer_result->to_balance},
        {"converted_amount", transfer_result->converted_amount}
    };
}

nlohmann::json CommandDispatcher::handle_get_history(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    uint64_t account_id;
    if (!extract_required(request, "account_id", account_id)) {
        return error_response("Missing field: account_id");
    }

    auto account = bank_.get_account(account_id);
    if (!account || account->user_id != *user_id) {
        return error_response("Account not found");
    }

    auto history = bank_.get_history(account_id);

    std::string filter_type = "all";
    if (request.contains("filter_type")) {
        if (!extract_required(request, "filter_type", filter_type)) {
            return error_response("Invalid field: filter_type");
        }

        if (filter_type != "all") {
            auto parsed = optype_from_string(filter_type);
            if (!parsed) {
                return error_response("Invalid filter_type");
            }

            history.erase(
                std::remove_if(history.begin(), history.end(),
                               [&](const HistoryEntry& entry) { return entry.type != *parsed; }),
                history.end());
        }
    }

    std::optional<int64_t> from_date;
    std::optional<int64_t> to_date;
    if (!extract_optional_epoch_seconds(request, "from_date", from_date) ||
        !extract_optional_epoch_seconds(request, "to_date", to_date)) {
        return error_response("Invalid field: from_date/to_date");
    }

    if (from_date && to_date && *from_date > *to_date) {
        return error_response("Invalid date range");
    }

    if (from_date || to_date) {
        history.erase(
            std::remove_if(history.begin(), history.end(),
                           [&](const HistoryEntry& entry) {
                               const int64_t ts = std::chrono::duration_cast<std::chrono::seconds>(
                                   entry.timestamp.time_since_epoch()).count();
                               if (from_date && ts < *from_date) return true;
                               if (to_date && ts > *to_date) return true;
                               return false;
                           }),
            history.end());
    }

    nlohmann::json out = nlohmann::json::array();
    for (const auto& entry : history) {
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            entry.timestamp.time_since_epoch()).count();
        out.push_back({
            {"timestamp", ts},
            {"op_type", to_string(entry.type)},
            {"amount", entry.amount},
            {"balance_after", entry.balance_after},
            {"counterparty", entry.counterparty}
        });
    }

    return {
        {"status", "ok"},
        {"history", out}
    };
}

nlohmann::json CommandDispatcher::handle_get_quotes(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    auto quotes = prices_.get_all_quotes();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [ticker, price] : quotes) {
        arr.push_back({{"ticker", ticker}, {"price", price}});
    }

    return {
        {"status", "ok"},
        {"quotes", arr}
    };
}

nlohmann::json CommandDispatcher::handle_get_exchange_rates(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    return {
        {"status", "ok"},
        {"rates", {
            {"USD_RUB", prices_.get_rate(Currency::USD, Currency::RUB)},
            {"EUR_RUB", prices_.get_rate(Currency::EUR, Currency::RUB)},
            {"USD_EUR", prices_.get_rate(Currency::USD, Currency::EUR)},
            {"RUB_USD", prices_.get_rate(Currency::RUB, Currency::USD)},
            {"RUB_EUR", prices_.get_rate(Currency::RUB, Currency::EUR)},
            {"EUR_USD", prices_.get_rate(Currency::EUR, Currency::USD)}
        }}
    };
}

nlohmann::json CommandDispatcher::handle_buy_stock(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    std::string ticker;
    int quantity;
    uint64_t account_id;
    if (!extract_required(request, "ticker", ticker) ||
        !extract_required(request, "quantity", quantity) ||
        !extract_required(request, "account_id", account_id)) {
        return error_response("Missing field: ticker/quantity/account_id");
    }

    auto result = stock_.buy(*user_id, ticker, quantity, account_id);
    if (!result) {
        return error_response("Buy failed");
    }

    return {
        {"status", "ok"},
        {"price", result->price},
        {"total_cost", result->total_cost},
        {"new_balance", result->new_balance}
    };
}

nlohmann::json CommandDispatcher::handle_sell_stock(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    std::string ticker;
    int quantity;
    uint64_t account_id;
    if (!extract_required(request, "ticker", ticker) ||
        !extract_required(request, "quantity", quantity) ||
        !extract_required(request, "account_id", account_id)) {
        return error_response("Missing field: ticker/quantity/account_id");
    }

    auto result = stock_.sell(*user_id, ticker, quantity, account_id);
    if (!result) {
        return error_response("Sell failed");
    }

    return {
        {"status", "ok"},
        {"price", result->price},
        {"total_revenue", result->total_revenue},
        {"new_balance", result->new_balance}
    };
}

nlohmann::json CommandDispatcher::handle_get_portfolio(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    auto positions = stock_.get_portfolio(*user_id);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& pos : positions) {
        const double current = prices_.get_quote(pos.ticker);
        const double pnl = (current - pos.avg_price) * static_cast<double>(pos.quantity);
        out.push_back({
            {"ticker", pos.ticker},
            {"quantity", pos.quantity},
            {"avg_price", pos.avg_price},
            {"current_price", current},
            {"pnl", pnl}
        });
    }

    return {
        {"status", "ok"},
        {"positions", out}
    };
}

nlohmann::json CommandDispatcher::handle_get_trades(const nlohmann::json& request) const {
    auto user_id = user_id_from_token(request);
    if (!user_id) return unauthorized();

    auto trades = stock_.get_trades(*user_id);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& trade : trades) {
        const auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            trade.timestamp.time_since_epoch()).count();
        out.push_back({
            {"timestamp", ts},
            {"ticker", trade.ticker},
            {"side", trade.is_buy ? "buy" : "sell"},
            {"quantity", trade.quantity},
            {"price", trade.price}
        });
    }

    return {
        {"status", "ok"},
        {"trades", out}
    };
}

nlohmann::json CommandDispatcher::unauthorized() const {
    return error_response("Invalid token");
}

nlohmann::json CommandDispatcher::error_response(const std::string& message) const {
    return {
        {"status", "error"},
        {"message", message}
    };
}

std::optional<uint64_t> CommandDispatcher::user_id_from_token(const nlohmann::json& request) const {
    std::string token;
    if (!extract_required(request, "token", token)) {
        return std::nullopt;
    }

    return auth_.validate(token);
}
