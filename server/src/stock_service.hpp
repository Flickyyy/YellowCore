#pragma once
#include "models.hpp"
#include "bank_service.hpp"
#include "price_engine.hpp"
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <memory>

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
};

// Per-user portfolio data with its own read-write lock
struct UserPortfolio {
    mutable std::shared_mutex mu;
    std::unordered_map<std::string, Position> positions;
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

private:
    std::shared_ptr<UserPortfolio> find_or_create(uint64_t user_id);
    std::shared_ptr<UserPortfolio> find_user(uint64_t user_id) const;

    IBankService& bank_;
    PriceEngine& prices_;

    mutable std::shared_mutex map_mu_;
    std::unordered_map<uint64_t, std::shared_ptr<UserPortfolio>> users_;
};
