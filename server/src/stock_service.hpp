#pragma once
#include "models.hpp"
#include "bank_service.hpp"
#include "price_engine.hpp"
#include "concurrent_map.hpp"
#include <vector>
#include <shared_mutex>
#include <optional>
#include <memory>
#include <unordered_map>

struct BuyResult  { double price; double total_cost; double new_balance; };
struct SellResult { double price; double total_revenue; double new_balance; };

class IStockService {
public:
    virtual ~IStockService() = default;
    virtual std::optional<BuyResult>  buy(uint64_t user_id, const std::string& ticker,
                                          int quantity, uint64_t account_id) = 0;
    virtual std::optional<SellResult> sell(uint64_t user_id, const std::string& ticker,
                                           int quantity, uint64_t account_id) = 0;
    virtual std::vector<Position> get_portfolio(uint64_t user_id) const = 0;
    virtual std::vector<Trade>    get_trades(uint64_t user_id) const = 0;
    virtual bool has_open_positions_on_account(uint64_t user_id, uint64_t account_id) const = 0;
};

struct UserPortfolio {
    mutable std::shared_mutex mu;
    std::unordered_map<std::string, Position> positions;
    std::unordered_map<uint64_t, std::unordered_map<std::string, int>> account_positions;
    std::vector<Trade> trades;
};

class StockService : public IStockService {
public:
    StockService(IBankService& bank, PriceEngine& prices);

    std::optional<BuyResult>  buy(uint64_t user_id, const std::string& ticker,
                                  int quantity, uint64_t account_id) override;
    std::optional<SellResult> sell(uint64_t user_id, const std::string& ticker,
                                   int quantity, uint64_t account_id) override;

    std::vector<Position> get_portfolio(uint64_t user_id) const override;
    std::vector<Trade>    get_trades(uint64_t user_id) const override;
    bool has_open_positions_on_account(uint64_t user_id, uint64_t account_id) const override;

private:
    IBankService& bank_;
    PriceEngine& prices_;
    ConcurrentMap<uint64_t, std::shared_ptr<UserPortfolio>> users_;
};
