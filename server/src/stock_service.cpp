#include "stock_service.hpp"

StockService::StockService(BankService& bank, PriceEngine& prices)
    : bank_(bank), prices_(prices) {}

std::optional<StockService::BuyResult> StockService::buy(
    uint64_t user_id, const std::string& ticker, int quantity, uint64_t account_id) {
    if (quantity <= 0) return std::nullopt;

    double price = prices_.get_quote(ticker);
    if (price <= 0) return std::nullopt;

    auto acc = bank_.get_account(account_id);
    if (!acc || acc->user_id != user_id) return std::nullopt;

    double rate = prices_.get_rate(Currency::USD, acc->currency);
    double cost_local = price * quantity * rate;

    // Debit account (bank locks internally)
    auto new_balance = bank_.debit_for_stock(user_id, account_id, cost_local, ticker);
    if (!new_balance) return std::nullopt;

    // Update portfolio (separate lock — no nesting)
    {
        std::lock_guard lock(mu_);
        auto& pos = portfolios_[user_id][ticker];
        if (pos.ticker.empty()) pos.ticker = ticker;
        double total = pos.avg_price * pos.quantity + price * quantity;
        pos.quantity += quantity;
        pos.avg_price = total / pos.quantity;
        trades_[user_id].push_back({std::chrono::system_clock::now(), ticker, true, quantity, price});
    }

    return BuyResult{price, cost_local, *new_balance};
}

std::optional<StockService::SellResult> StockService::sell(
    uint64_t user_id, const std::string& ticker, int quantity, uint64_t account_id) {
    if (quantity <= 0) return std::nullopt;

    double price = prices_.get_quote(ticker);
    if (price <= 0) return std::nullopt;

    // Reserve shares (decrement immediately to prevent double-sell)
    {
        std::lock_guard lock(mu_);
        auto uit = portfolios_.find(user_id);
        if (uit == portfolios_.end()) return std::nullopt;
        auto pit = uit->second.find(ticker);
        if (pit == uit->second.end() || pit->second.quantity < quantity)
            return std::nullopt;
        pit->second.quantity -= quantity;
    }

    auto acc = bank_.get_account(account_id);
    if (!acc || acc->user_id != user_id) {
        std::lock_guard lock(mu_);
        portfolios_[user_id][ticker].quantity += quantity;  // undo reservation
        return std::nullopt;
    }

    double rate = prices_.get_rate(Currency::USD, acc->currency);
    double revenue_local = price * quantity * rate;

    auto new_balance = bank_.credit_for_stock(user_id, account_id, revenue_local, ticker);
    if (!new_balance) {
        std::lock_guard lock(mu_);
        portfolios_[user_id][ticker].quantity += quantity;  // undo reservation
        return std::nullopt;
    }

    // Finalize: record trade, cleanup empty positions
    {
        std::lock_guard lock(mu_);
        auto& pos = portfolios_[user_id][ticker];
        if (pos.quantity == 0) {
            portfolios_[user_id].erase(ticker);
            if (portfolios_[user_id].empty()) portfolios_.erase(user_id);
        }
        trades_[user_id].push_back({std::chrono::system_clock::now(), ticker, false, quantity, price});
    }

    return SellResult{price, revenue_local, *new_balance};
}

std::vector<Position> StockService::get_portfolio(uint64_t user_id) const {
    std::lock_guard lock(mu_);
    std::vector<Position> result;
    auto it = portfolios_.find(user_id);
    if (it == portfolios_.end()) return result;
    for (auto& [_, pos] : it->second)
        result.push_back(pos);
    return result;
}

std::vector<Trade> StockService::get_trades(uint64_t user_id) const {
    std::lock_guard lock(mu_);
    auto it = trades_.find(user_id);
    if (it == trades_.end()) return {};
    return it->second;
}
