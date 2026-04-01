#include "stock_service.hpp"

StockService::StockService(IBankService& bank, PriceEngine& prices)
    : bank_(bank), prices_(prices) {}

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

    auto up = users_.get_or_create(user_id, [] { return std::make_shared<UserPortfolio>(); });
    {
        std::unique_lock lk(up->mu);
        auto& pos = up->positions[ticker];
        if (pos.ticker.empty()) pos.ticker = ticker;
        double total = pos.avg_price * pos.quantity + price * quantity;
        pos.quantity += quantity;
        pos.avg_price = total / pos.quantity;
        up->account_positions[account_id][ticker] += quantity;
        up->trades.push_back({std::chrono::system_clock::now(), ticker, true, quantity, price});
    }

    return BuyResult{price, cost_local, *new_balance};
}

std::optional<SellResult> StockService::sell(
    uint64_t user_id, const std::string& ticker, int quantity, uint64_t account_id) {
    if (quantity <= 0) return std::nullopt;

    double price = prices_.get_quote(ticker);
    if (price <= 0) return std::nullopt;

    auto acc = bank_.get_account(account_id);
    if (!acc || acc->user_id != user_id) return std::nullopt;

    auto up_opt = users_.get(user_id);
    if (!up_opt) return std::nullopt;
    auto& up = *up_opt;
    {
        std::unique_lock lk(up->mu);
        auto pit = up->positions.find(ticker);
        if (pit == up->positions.end() || pit->second.quantity < quantity) {
            return std::nullopt;
        }

        auto account_it = up->account_positions.find(account_id);
        if (account_it == up->account_positions.end()) {
            return std::nullopt;
        }

        auto lot_it = account_it->second.find(ticker);
        if (lot_it == account_it->second.end() || lot_it->second < quantity) {
            return std::nullopt;
        }

        pit->second.quantity -= quantity;
        lot_it->second -= quantity;
        if (lot_it->second == 0) {
            account_it->second.erase(lot_it);
        }
        if (account_it->second.empty()) {
            up->account_positions.erase(account_it);
        }
    }

    double rate = prices_.get_rate(Currency::USD, acc->currency);
    double revenue_local = price * quantity * rate;

    auto new_balance = bank_.credit_for_stock(user_id, account_id, revenue_local, ticker);
    if (!new_balance) {
        std::unique_lock lk(up->mu);
        up->positions[ticker].quantity += quantity;
        up->account_positions[account_id][ticker] += quantity;
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
    auto up_opt = users_.get(user_id);
    if (!up_opt) return {};
    auto& up = *up_opt;
    std::shared_lock lk(up->mu);
    std::vector<Position> result;
    for (auto& [_, pos] : up->positions)
        result.push_back(pos);
    return result;
}

std::vector<Trade> StockService::get_trades(uint64_t user_id) const {
    auto up_opt = users_.get(user_id);
    if (!up_opt) return {};
    auto& up = *up_opt;
    std::shared_lock lk(up->mu);
    return up->trades;
}

bool StockService::has_open_positions_on_account(uint64_t user_id, uint64_t account_id) const {
    auto up_opt = users_.get(user_id);
    if (!up_opt) return false;

    auto& up = *up_opt;
    std::shared_lock lk(up->mu);
    auto account_it = up->account_positions.find(account_id);
    if (account_it == up->account_positions.end()) {
        return false;
    }

    for (const auto& [_, qty] : account_it->second) {
        if (qty > 0) {
            return true;
        }
    }
    return false;
}
