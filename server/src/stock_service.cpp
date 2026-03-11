#include "stock_service.hpp"

StockService::StockService(IBankService& bank, PriceEngine& prices)
    : bank_(bank), prices_(prices) {}

std::shared_ptr<UserPortfolio> StockService::find_or_create(uint64_t user_id) {
    {
        std::shared_lock lk(map_mu_);
        auto it = users_.find(user_id);
        if (it != users_.end()) return it->second;
    }
    std::unique_lock lk(map_mu_);
    auto& ptr = users_[user_id];
    if (!ptr) ptr = std::make_shared<UserPortfolio>();
    return ptr;
}

std::shared_ptr<UserPortfolio> StockService::find_user(uint64_t user_id) const {
    std::shared_lock lk(map_mu_);
    auto it = users_.find(user_id);
    return it != users_.end() ? it->second : nullptr;
}

std::optional<BuyResult> StockService::buy(
    uint64_t user_id, const std::string& ticker, int quantity, uint64_t account_id) {
    if (quantity <= 0) return std::nullopt;

    double price = prices_.get_quote(ticker);
    if (price <= 0) return std::nullopt;

    auto acc = bank_.get_account(account_id);
    if (!acc || acc->user_id != user_id) return std::nullopt;

    double rate = prices_.get_rate(Currency::USD, acc->currency);
    double cost_local = price * quantity * rate;

    auto new_balance = bank_.debit_for_stock(user_id, account_id, cost_local, ticker);
    if (!new_balance) return std::nullopt;

    auto up = find_or_create(user_id);
    {
        std::unique_lock lk(up->mu);
        auto& pos = up->positions[ticker];
        if (pos.ticker.empty()) pos.ticker = ticker;
        double total = pos.avg_price * pos.quantity + price * quantity;
        pos.quantity += quantity;
        pos.avg_price = total / pos.quantity;
        up->trades.push_back({std::chrono::system_clock::now(), ticker, true, quantity, price});
    }

    return BuyResult{price, cost_local, *new_balance};
}

std::optional<SellResult> StockService::sell(
    uint64_t user_id, const std::string& ticker, int quantity, uint64_t account_id) {
    if (quantity <= 0) return std::nullopt;

    double price = prices_.get_quote(ticker);
    if (price <= 0) return std::nullopt;

    // Reserve shares (per-user lock)
    auto up = find_user(user_id);
    if (!up) return std::nullopt;
    {
        std::unique_lock lk(up->mu);
        auto pit = up->positions.find(ticker);
        if (pit == up->positions.end() || pit->second.quantity < quantity)
            return std::nullopt;
        pit->second.quantity -= quantity;
    }

    auto acc = bank_.get_account(account_id);
    if (!acc || acc->user_id != user_id) {
        std::unique_lock lk(up->mu);
        up->positions[ticker].quantity += quantity;
        return std::nullopt;
    }

    double rate = prices_.get_rate(Currency::USD, acc->currency);
    double revenue_local = price * quantity * rate;

    auto new_balance = bank_.credit_for_stock(user_id, account_id, revenue_local, ticker);
    if (!new_balance) {
        std::unique_lock lk(up->mu);
        up->positions[ticker].quantity += quantity;
        return std::nullopt;
    }

    {
        std::unique_lock lk(up->mu);
        auto& pos = up->positions[ticker];
        if (pos.quantity == 0) {
            up->positions.erase(ticker);
        }
        up->trades.push_back({std::chrono::system_clock::now(), ticker, false, quantity, price});
    }

    return SellResult{price, revenue_local, *new_balance};
}

std::vector<Position> StockService::get_portfolio(uint64_t user_id) const {
    auto up = find_user(user_id);
    if (!up) return {};
    std::shared_lock lk(up->mu);
    std::vector<Position> result;
    for (auto& [_, pos] : up->positions)
        result.push_back(pos);
    return result;
}

std::vector<Trade> StockService::get_trades(uint64_t user_id) const {
    auto up = find_user(user_id);
    if (!up) return {};
    std::shared_lock lk(up->mu);
    return up->trades;
}
