#include "command_dispatcher.hpp"

#include <chrono>

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
    if (type == "deposit") return handle_deposit(request);
    if (type == "withdraw") return handle_withdraw(request);
    if (type == "transfer") return handle_transfer(request);
    if (type == "get_history") return handle_get_history(request);
    if (type == "get_quotes") return handle_get_quotes(request);
    if (type == "get_exchange_rates") return handle_get_exchange_rates(request);

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
