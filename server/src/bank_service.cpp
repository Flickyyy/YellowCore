#include "bank_service.hpp"
#include <cmath>

uint64_t BankService::create_account(uint64_t user_id, Currency currency) {
    std::lock_guard lock(mu_);
    uint64_t id = next_id_++;
    accounts_[id] = Account{id, user_id, currency, 0.0};
    return id;
}

bool BankService::close_account(uint64_t user_id, uint64_t account_id) {
    std::lock_guard lock(mu_);
    auto it = accounts_.find(account_id);
    if (it == accounts_.end() || it->second.user_id != user_id) return false;
    if (std::abs(it->second.balance) > 1e-9) return false;
    accounts_.erase(it);
    history_.erase(account_id);
    return true;
}

std::vector<Account> BankService::get_accounts(uint64_t user_id) const {
    std::lock_guard lock(mu_);
    std::vector<Account> result;
    for (auto& [_, acc] : accounts_)
        if (acc.user_id == user_id) result.push_back(acc);
    return result;
}

std::optional<Account> BankService::get_account(uint64_t account_id) const {
    std::lock_guard lock(mu_);
    auto it = accounts_.find(account_id);
    if (it == accounts_.end()) return std::nullopt;
    return it->second;
}

std::optional<double> BankService::deposit(uint64_t user_id, uint64_t account_id, double amount) {
    if (amount <= 0) return std::nullopt;
    std::lock_guard lock(mu_);
    auto it = accounts_.find(account_id);
    if (it == accounts_.end() || it->second.user_id != user_id) return std::nullopt;
    it->second.balance += amount;
    record(account_id, OpType::Deposit, amount, it->second.balance);
    return it->second.balance;
}

std::optional<double> BankService::withdraw(uint64_t user_id, uint64_t account_id, double amount) {
    if (amount <= 0) return std::nullopt;
    std::lock_guard lock(mu_);
    auto it = accounts_.find(account_id);
    if (it == accounts_.end() || it->second.user_id != user_id) return std::nullopt;
    if (it->second.balance < amount) return std::nullopt;
    it->second.balance -= amount;
    record(account_id, OpType::Withdraw, amount, it->second.balance);
    return it->second.balance;
}

std::optional<BankService::TransferResult> BankService::transfer(
    uint64_t user_id, uint64_t from_id, uint64_t to_id, double amount, double rate) {
    if (amount <= 0 || rate <= 0) return std::nullopt;
    if (from_id == to_id) return std::nullopt;
    std::lock_guard lock(mu_);
    auto from_it = accounts_.find(from_id);
    auto to_it = accounts_.find(to_id);
    if (from_it == accounts_.end() || from_it->second.user_id != user_id) return std::nullopt;
    if (to_it == accounts_.end()) return std::nullopt;
    if (from_it->second.balance < amount) return std::nullopt;

    double converted = amount * rate;
    from_it->second.balance -= amount;
    to_it->second.balance += converted;

    record(from_id, OpType::TransferOut, amount, from_it->second.balance,
           "-> account " + std::to_string(to_id));
    record(to_id, OpType::TransferIn, converted, to_it->second.balance,
           "<- account " + std::to_string(from_id));

    return TransferResult{from_it->second.balance, to_it->second.balance, converted};
}

std::optional<double> BankService::debit_for_stock(uint64_t user_id, uint64_t account_id,
                                                    double amount, const std::string& ticker) {
    if (amount <= 0) return std::nullopt;
    std::lock_guard lock(mu_);
    auto it = accounts_.find(account_id);
    if (it == accounts_.end() || it->second.user_id != user_id) return std::nullopt;
    if (it->second.balance < amount) return std::nullopt;
    it->second.balance -= amount;
    record(account_id, OpType::BuyStock, amount, it->second.balance, ticker);
    return it->second.balance;
}

std::optional<double> BankService::credit_for_stock(uint64_t user_id, uint64_t account_id,
                                                     double amount, const std::string& ticker) {
    if (amount <= 0) return std::nullopt;
    std::lock_guard lock(mu_);
    auto it = accounts_.find(account_id);
    if (it == accounts_.end() || it->second.user_id != user_id) return std::nullopt;
    it->second.balance += amount;
    record(account_id, OpType::SellStock, amount, it->second.balance, ticker);
    return it->second.balance;
}

std::vector<HistoryEntry> BankService::get_history(uint64_t account_id) const {
    std::lock_guard lock(mu_);
    auto it = history_.find(account_id);
    if (it == history_.end()) return {};
    return it->second;
}

void BankService::record(uint64_t account_id, OpType type, double amount,
                          double balance_after, const std::string& counterparty) {
    history_[account_id].push_back({
        std::chrono::system_clock::now(), type, amount, balance_after, counterparty
    });
}
