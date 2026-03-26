#pragma once

#include "auth_service.hpp"
#include "bank_service.hpp"
#include "price_engine.hpp"
#include "stock_service.hpp"

#include <nlohmann/json.hpp>

class CommandDispatcher {
public:
    CommandDispatcher(IAuthService& auth, IBankService& bank, IStockService& stock, PriceEngine& prices);

    nlohmann::json handle_message(const nlohmann::json& request) const;

private:
    nlohmann::json handle_register(const nlohmann::json& request) const;
    nlohmann::json handle_login(const nlohmann::json& request) const;
    nlohmann::json handle_logout(const nlohmann::json& request) const;

    nlohmann::json handle_create_account(const nlohmann::json& request) const;
    nlohmann::json handle_get_accounts(const nlohmann::json& request) const;
    nlohmann::json handle_deposit(const nlohmann::json& request) const;
    nlohmann::json handle_withdraw(const nlohmann::json& request) const;
    nlohmann::json handle_transfer(const nlohmann::json& request) const;
    nlohmann::json handle_get_history(const nlohmann::json& request) const;

    nlohmann::json handle_get_quotes(const nlohmann::json& request) const;
    nlohmann::json handle_get_exchange_rates(const nlohmann::json& request) const;

    nlohmann::json unauthorized() const;
    nlohmann::json error_response(const std::string& message) const;

    std::optional<uint64_t> user_id_from_token(const nlohmann::json& request) const;

    IAuthService& auth_;
    IBankService& bank_;
    [[maybe_unused]] IStockService& stock_;
    PriceEngine& prices_;
};
