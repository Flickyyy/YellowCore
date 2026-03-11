#include "bank_service.hpp"
#include <cmath>
#include <mutex>

void BankService::record(Account& acc, OpType type, double amount, const std::string& counterparty) {
    acc.history.push_back({
        std::chrono::system_clock::now(), type, amount, acc.balance, counterparty
    });
}

// --- structural ops (stripe-level locks only) ---

uint64_t BankService::create_account(uint64_t user_id, Currency currency) {
    uint64_t id = next_id_++;
    auto ud = users_.get_or_create(user_id, [] { return std::make_shared<UserAccounts>(); });
    account_index_.put(id, user_id);
    std::unique_lock lk(ud->mu);
    ud->accounts[id] = Account{id, user_id, currency, 0.0, {}};
    return id;
}

bool BankService::close_account(uint64_t user_id, uint64_t account_id) {
    auto owner = account_index_.get(account_id);
    if (!owner || *owner != user_id) return false;
    auto ud_opt = users_.get(user_id);
    if (!ud_opt) return false;
    auto& ud = *ud_opt;
    std::unique_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end()) return false;
    if (std::abs(it->second.balance) > 1e-9) return false;
    ud->accounts.erase(it);
    account_index_.erase(account_id);
    return true;
}

// --- read ops (stripe-level shared → per-user shared) ---

std::vector<Account> BankService::get_accounts(uint64_t user_id) const {
    auto ud_opt = users_.get(user_id);
    if (!ud_opt) return {};
    auto& ud = *ud_opt;
    std::shared_lock lk(ud->mu);
    std::vector<Account> result;
    for (auto& [_, acc] : ud->accounts)
        result.push_back(acc);
    return result;
}

std::optional<Account> BankService::get_account(uint64_t account_id) const {
    auto owner = account_index_.get(account_id);
    if (!owner) return std::nullopt;
    auto ud_opt = users_.get(*owner);
    if (!ud_opt) return std::nullopt;
    auto& ud = *ud_opt;
    std::shared_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end()) return std::nullopt;
    return it->second;
}

std::vector<HistoryEntry> BankService::get_history(uint64_t account_id) const {
    auto owner = account_index_.get(account_id);
    if (!owner) return {};
    auto ud_opt = users_.get(*owner);
    if (!ud_opt) return {};
    auto& ud = *ud_opt;
    std::shared_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end()) return {};
    return it->second.history;
}

// --- write ops (stripe-level shared → per-user unique) ---

std::optional<double> BankService::deposit(uint64_t user_id, uint64_t account_id, double amount) {
    if (amount <= 0) return std::nullopt;
    auto ud_opt = users_.get(user_id);
    if (!ud_opt) return std::nullopt;
    auto& ud = *ud_opt;
    std::unique_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end() || it->second.user_id != user_id) return std::nullopt;
    it->second.balance += amount;
    record(it->second, OpType::Deposit, amount);
    return it->second.balance;
}

std::optional<double> BankService::withdraw(uint64_t user_id, uint64_t account_id, double amount) {
    if (amount <= 0) return std::nullopt;
    auto ud_opt = users_.get(user_id);
    if (!ud_opt) return std::nullopt;
    auto& ud = *ud_opt;
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

    auto from_owner = account_index_.get(from_id);
    auto to_owner = account_index_.get(to_id);
    if (!from_owner || !to_owner || *from_owner != user_id) return std::nullopt;

    auto from_ud_opt = users_.get(*from_owner);
    auto to_ud_opt = users_.get(*to_owner);
    if (!from_ud_opt || !to_ud_opt) return std::nullopt;
    auto& from_ud = *from_ud_opt;
    auto& to_ud = *to_ud_opt;

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

    if (*from_owner == *to_owner) {
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
    auto ud_opt = users_.get(user_id);
    if (!ud_opt) return std::nullopt;
    auto& ud = *ud_opt;
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
    auto ud_opt = users_.get(user_id);
    if (!ud_opt) return std::nullopt;
    auto& ud = *ud_opt;
    std::unique_lock lk(ud->mu);
    auto it = ud->accounts.find(account_id);
    if (it == ud->accounts.end() || it->second.user_id != user_id) return std::nullopt;
    it->second.balance += amount;
    record(it->second, OpType::SellStock, amount, ticker);
    return it->second.balance;
}
