#pragma once
#include "models.hpp"
#include "bank_service.hpp"
#include "price_engine.hpp"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>

class StockService {
public:
    StockService(BankService& bank, PriceEngine& prices);

    struct BuyResult  { double price; double total_cost; double new_balance; };
    struct SellResult { double price; double total_revenue; double new_balance; };

    std::optional<BuyResult>  buy(uint64_t user_id, const std::string& ticker,
                                  int quantity, uint64_t account_id);
    std::optional<SellResult> sell(uint64_t user_id, const std::string& ticker,
                                   int quantity, uint64_t account_id);

    std::vector<Position> get_portfolio(uint64_t user_id) const;
    std::vector<Trade>    get_trades(uint64_t user_id) const;

private:
    BankService& bank_;
    PriceEngine& prices_;

    mutable std::mutex mu_;
    std::unordered_map<uint64_t, std::unordered_map<std::string, Position>> portfolios_;
    std::unordered_map<uint64_t, std::vector<Trade>> trades_;
};
