#pragma once
#include "models.hpp"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>
#include <atomic>

class BankService {
public:
    uint64_t create_account(uint64_t user_id, Currency currency);
    bool close_account(uint64_t user_id, uint64_t account_id);
    std::vector<Account> get_accounts(uint64_t user_id) const;
    std::optional<Account> get_account(uint64_t account_id) const;

    std::optional<double> deposit(uint64_t user_id, uint64_t account_id, double amount);
    std::optional<double> withdraw(uint64_t user_id, uint64_t account_id, double amount);

    struct TransferResult { double from_balance; double to_balance; double converted_amount; };
    std::optional<TransferResult> transfer(uint64_t user_id, uint64_t from_id,
                                           uint64_t to_id, double amount, double rate = 1.0);

    std::optional<double> debit_for_stock(uint64_t user_id, uint64_t account_id,
                                           double amount, const std::string& ticker);
    std::optional<double> credit_for_stock(uint64_t user_id, uint64_t account_id,
                                            double amount, const std::string& ticker);

    std::vector<HistoryEntry> get_history(uint64_t account_id) const;

private:
    void record(uint64_t account_id, OpType type, double amount,
                double balance_after, const std::string& counterparty = "");

    mutable std::mutex mu_;
    std::unordered_map<uint64_t, Account> accounts_;
    std::unordered_map<uint64_t, std::vector<HistoryEntry>> history_;
    std::atomic<uint64_t> next_id_{100001};
};
