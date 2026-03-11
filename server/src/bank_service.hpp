#pragma once
#include "models.hpp"
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <optional>
#include <atomic>
#include <memory>

struct TransferResult {
    double from_balance;
    double to_balance;
    double converted_amount;
};

class IBankService {
public:
    virtual ~IBankService() = default;
    virtual uint64_t create_account(uint64_t user_id, Currency currency) = 0;
    virtual bool close_account(uint64_t user_id, uint64_t account_id) = 0;
    virtual std::vector<Account> get_accounts(uint64_t user_id) const = 0;
    virtual std::optional<Account> get_account(uint64_t account_id) const = 0;

    virtual std::optional<double> deposit(uint64_t user_id, uint64_t account_id, double amount) = 0;
    virtual std::optional<double> withdraw(uint64_t user_id, uint64_t account_id, double amount) = 0;
    virtual std::optional<TransferResult> transfer(uint64_t user_id, uint64_t from_id,
                                                   uint64_t to_id, double amount, double rate = 1.0) = 0;

    virtual std::optional<double> debit_for_stock(uint64_t user_id, uint64_t account_id,
                                                   double amount, const std::string& ticker) = 0;
    virtual std::optional<double> credit_for_stock(uint64_t user_id, uint64_t account_id,
                                                    double amount, const std::string& ticker) = 0;

    virtual std::vector<HistoryEntry> get_history(uint64_t account_id) const = 0;
};

// Per-user account data with its own read-write lock
struct UserAccounts {
    mutable std::shared_mutex mu;
    std::unordered_map<uint64_t, Account> accounts;
};

class BankService : public IBankService {
public:
    uint64_t create_account(uint64_t user_id, Currency currency) override;
    bool close_account(uint64_t user_id, uint64_t account_id) override;
    std::vector<Account> get_accounts(uint64_t user_id) const override; 
    std::optional<Account> get_account(uint64_t account_id) const override;

    std::optional<double> deposit(uint64_t user_id, uint64_t account_id, double amount) override;
    std::optional<double> withdraw(uint64_t user_id, uint64_t account_id, double amount) override;
    std::optional<TransferResult> transfer(uint64_t user_id, uint64_t from_id,
                                           uint64_t to_id, double amount, double rate = 1.0) override;

    std::optional<double> debit_for_stock(uint64_t user_id, uint64_t account_id,
                                           double amount, const std::string& ticker) override;
    std::optional<double> credit_for_stock(uint64_t user_id, uint64_t account_id,
                                            double amount, const std::string& ticker) override;

    std::vector<HistoryEntry> get_history(uint64_t account_id) const override;

private:
    std::shared_ptr<UserAccounts> find_user(uint64_t user_id) const;
    std::pair<std::shared_ptr<UserAccounts>, uint64_t> find_user_by_account(uint64_t account_id) const;
    static void record(Account& acc, OpType type, double amount, const std::string& counterparty = "");

    mutable std::shared_mutex map_mu_;
    std::unordered_map<uint64_t, std::shared_ptr<UserAccounts>> users_;
    std::unordered_map<uint64_t, uint64_t> account_index_;  // account_id → user_id
    std::atomic<uint64_t> next_id_{100001};
};
