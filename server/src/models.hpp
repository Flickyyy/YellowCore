#pragma once
#include <string>
#include <cstdint>
#include <chrono>
#include <optional>

enum class Currency { RUB, USD, EUR };
enum class OpType { Deposit, Withdraw, TransferIn, TransferOut, BuyStock, SellStock };

inline std::string to_string(Currency c) {
    switch (c) {
        case Currency::RUB: return "RUB";
        case Currency::USD: return "USD";
        case Currency::EUR: return "EUR";
    }
    return "???";
}

inline std::optional<Currency> currency_from_string(const std::string& s) {
    if (s == "RUB") return Currency::RUB;
    if (s == "USD") return Currency::USD;
    if (s == "EUR") return Currency::EUR;
    return std::nullopt;
}

inline std::string to_string(OpType op) {
    switch (op) {
        case OpType::Deposit:     return "deposit";
        case OpType::Withdraw:    return "withdraw";
        case OpType::TransferIn:  return "transfer_in";
        case OpType::TransferOut: return "transfer_out";
        case OpType::BuyStock:    return "buy_stock";
        case OpType::SellStock:   return "sell_stock";
    }
    return "???";
}

inline std::optional<OpType> optype_from_string(const std::string& s) {
    if (s == "deposit")      return OpType::Deposit;
    if (s == "withdraw")     return OpType::Withdraw;
    if (s == "transfer_in")  return OpType::TransferIn;
    if (s == "transfer_out") return OpType::TransferOut;
    if (s == "buy_stock")    return OpType::BuyStock;
    if (s == "sell_stock")   return OpType::SellStock;
    return std::nullopt;
}

struct User {
    uint64_t id = 0;
    std::string username;
    std::string password_hash;
};

struct Account {
    uint64_t id = 0;
    uint64_t user_id = 0;
    Currency currency = Currency::RUB;
    double balance = 0.0;
};

using TimePoint = std::chrono::system_clock::time_point;

struct HistoryEntry {
    TimePoint timestamp;
    OpType type;
    double amount;
    double balance_after;
    std::string counterparty;
};

struct Position {
    std::string ticker;
    int quantity = 0;
    double avg_price = 0.0;
};

struct Trade {
    TimePoint timestamp;
    std::string ticker;
    bool is_buy;
    int quantity;
    double price;
};
