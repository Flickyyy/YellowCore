#include "bank_service.hpp"
#include <cmath>
#include <mutex>

// --- helpers ---

std::shared_ptr<UserAccounts> BankService::find_user(uint64_t user_id) const {
    std::shared_lock lk(map_mu_);
    auto it = users_.find(user_id);
    return it != users_.end() ? it->second : nullptr;
}

std::pair<std::shared_ptr<UserAccounts>, uint64_t>
BankService::find_user_by_account(uint64_t account_id) const {
    std::shared_lock lk(map_mu_);
    auto idx = account_index_.find(account_id);
    if (idx == account_index_.end()) return {nullptr, 0};
    auto uit = users_.find(idx->second);
    if (uit == users_.end()) return {nullptr, 0};
    return {uit->second, idx->second};
}

void BankService::record(Account& acc, OpType type, double amount, const std::string& counterparty) {
    acc.history.push_back({
        std::chrono::system_clock::now(), type, amount, acc.balance, counterparty
    });
}

// --- structural ops (unique map lock) ---

uint64_t BankService::create_account(uint64_t user_id, Currency currency) {
    uint64_t id = next_id_++;
    std::unique_lock map_lk(map_mu_);
    auto& ud = users_[user_id];
    if (!ud) ud = std::make_shared<UserAccounts>();
    account_index_[id] = user_id;
    std::unique_lock user_lk(ud->mu);
    ud->accounts[id] = Account{id, user_id, currency, 0.0, {}};
    return id;
}

bool BankService::close_account(uint64_t user_id, uint64_t account_id) {
    std::unique_lock map_lk(map_mu_);
    auto idx_it = account_index_.find(account_id);
    if (idx_it == account_index_.end() || idx_it->second != user_id) return false;
    auto uit = users_.find(user_id);
    if (uit == users_.end()) return false;
    auto& ud = uit->second;
    std::unique_lock user_lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end()) return false;
    if (std::abs(it->second.balance) > 1e-9) return false;
    ud->accounts.erase(it);
    account_index_.erase(idx_it);
    if (ud->accounts.empty()) users_.erase(uit);
    return true;
}

// --- read ops (shared map lock → shared user lock) ---

std::vector<Account> BankService::get_accounts(uint64_t user_id) const {
    auto ud = find_user(user_id);
    if (!ud) return {};
    std::shared_lock lk(ud->mu);
    std::vector<Account> result;
    for (auto& [_, acc] : ud->accounts)
        result.push_back(acc);
    return result;
}

std::optional<Account> BankService::get_account(uint64_t account_id) const {
    auto [ud, uid] = find_user_by_account(account_id);
    if (!ud) return std::nullopt;
    std::shared_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end()) return std::nullopt;
    return it->second;
}

std::vector<HistoryEntry> BankService::get_history(uint64_t account_id) const {
    auto [ud, uid] = find_user_by_account(account_id);
    if (!ud) return {};
    std::shared_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end()) return {};
    return it->second.history;
}

// --- write ops (shared map lock → unique user lock) ---

std::optional<double> BankService::deposit(uint64_t user_id, uint64_t account_id, double amount) {
    if (amount <= 0) return std::nullopt;
    auto ud = find_user(user_id);
    if (!ud) return std::nullopt;
    std::unique_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end() || it->second.user_id != user_id) return std::nullopt;
    it->second.balance += amount;
    record(it->second, OpType::Deposit, amount);
    return it->second.balance;
}

std::optional<double> BankService::withdraw(uint64_t user_id, uint64_t account_id, double amount) {
    if (amount <= 0) return std::nullopt;
    auto ud = find_user(user_id);
    if (!ud) return std::nullopt;
    std::unique_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end() || it->second.user_id != user_id) return std::nullopt;
    if (it->second.balance < amount) return std::nullopt;
    it->second.balance -= amount;
    record(it->second, OpType::Withdraw, amount);
    return it->second.balance;
}

std::optional<TransferResult> BankService::transfer(
    uint64_t user_id, uint64_t from_id, uint64_t to_id, double amount, double rate) {
    if (amount <= 0 || rate <= 0 || from_id == to_id) return std::nullopt;

    std::shared_ptr<UserAccounts> from_ud, to_ud;
    uint64_t from_uid, to_uid;
    {
        std::shared_lock map_lk(map_mu_);
        auto fi = account_index_.find(from_id);
        auto ti = account_index_.find(to_id);
        if (fi == account_index_.end() || ti == account_index_.end()) return std::nullopt;
        from_uid = fi->second;
        to_uid = ti->second;
        if (from_uid != user_id) return std::nullopt;
        from_ud = users_.at(from_uid);
        to_ud = users_.at(to_uid);
    }

    auto do_transfer = [&]() -> std::optional<TransferResult> {
        auto from_it = from_ud->accounts.find(from_id);
        auto to_it = to_ud->accounts.find(to_id);
        if (from_it == from_ud->accounts.end() || to_it == to_ud->accounts.end())
            return std::nullopt;
        if (from_it->second.balance < amount) return std::nullopt;

        double converted = amount * rate;
        from_it->second.balance -= amount;
        to_it->second.balance += converted;

        record(from_it->second, OpType::TransferOut, amount,
               "-> account " + std::to_string(to_id));
        record(to_it->second, OpType::TransferIn, converted,
               "<- account " + std::to_string(from_id));

        return TransferResult{from_it->second.balance, to_it->second.balance, converted};
    };

    if (from_uid == to_uid) {
        std::unique_lock lk(from_ud->mu);
        return do_transfer();
    } else {
        std::scoped_lock lk(from_ud->mu, to_ud->mu);
        return do_transfer();
    }
}

std::optional<double> BankService::debit_for_stock(uint64_t user_id, uint64_t account_id,
                                                    double amount, const std::string& ticker) {
    if (amount <= 0) return std::nullopt;
    auto ud = find_user(user_id);
    if (!ud) return std::nullopt;
    std::unique_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end() || it->second.user_id != user_id) return std::nullopt;
    if (it->second.balance < amount) return std::nullopt;
    it->second.balance -= amount;
    record(it->second, OpType::BuyStock, amount, ticker);
    return it->second.balance;
}

std::optional<double> BankService::credit_for_stock(uint64_t user_id, uint64_t account_id,
                                                     double amount, const std::string& ticker) {
    if (amount <= 0) return std::nullopt;
    auto ud = find_user(user_id);
    if (!ud) return std::nullopt;
    std::unique_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end() || it->second.user_id != user_id) return std::nullopt;
    it->second.balance += amount;
    record(it->second, OpType::SellStock, amount, ticker);
    return it->second.balance;
}
