#include <gtest/gtest.h>
#include "auth_service.hpp"
#include "bank_service.hpp"
#include "stock_service.hpp"
#include <thread>
#include <vector>
#include <atomic>

TEST(Concurrent, ParallelDeposits) {
    BankService bank;
    auto acc = bank.create_account(1, Currency::RUB);
    std::vector<std::thread> threads;
    for (int i = 0; i < 100; i++)
        threads.emplace_back([&] { bank.deposit(1, acc, 10.0); });
    for (auto& t : threads) t.join();
    EXPECT_DOUBLE_EQ(bank.get_account(acc)->balance, 1000.0);
}

TEST(Concurrent, ParallelWithdraws) {
    BankService bank;
    auto acc = bank.create_account(1, Currency::RUB);
    bank.deposit(1, acc, 1000.0);
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 200; i++)
        threads.emplace_back([&] { if (bank.withdraw(1, acc, 10.0)) ok++; });
    for (auto& t : threads) t.join();
    EXPECT_EQ(ok.load(), 100);
    EXPECT_DOUBLE_EQ(bank.get_account(acc)->balance, 0.0);
}

TEST(Concurrent, TransferConservation) {
    BankService bank;
    auto a = bank.create_account(1, Currency::RUB);
    auto b = bank.create_account(1, Currency::RUB);
    bank.deposit(1, a, 5000);
    bank.deposit(1, b, 5000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 100; i++) {
        threads.emplace_back([&] { bank.transfer(1, a, b, 10.0); });
        threads.emplace_back([&] { bank.transfer(1, b, a, 10.0); });
    }
    for (auto& t : threads) t.join();
    double total = bank.get_account(a)->balance + bank.get_account(b)->balance;
    EXPECT_DOUBLE_EQ(total, 10000.0);
}

TEST(Concurrent, PriceEngineReads) {
    PriceEngine prices;
    prices.start();
    std::atomic<int> reads{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++)
        threads.emplace_back([&] {
            for (int j = 0; j < 100; j++) {
                EXPECT_FALSE(prices.get_all_quotes().empty());
                reads++;
            }
        });
    for (auto& t : threads) t.join();
    prices.stop();
    EXPECT_EQ(reads.load(), 1000);
}

TEST(Concurrent, ParallelRegistrations) {
    AuthService auth;
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 100; i++)
        threads.emplace_back([&, i] {
            if (auth.register_user("user" + std::to_string(i), "pass")) ok++;
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(ok.load(), 100);
}

TEST(Concurrent, ParallelStockBuys) {
    BankService bank;
    PriceEngine prices;
    StockService stocks(bank, prices);
    auto acc = bank.create_account(1, Currency::USD);
    bank.deposit(1, acc, 1000000);
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 50; i++)
        threads.emplace_back([&] { if (stocks.buy(1, "AAPL", 1, acc)) ok++; });
    for (auto& t : threads) t.join();
    EXPECT_EQ(ok.load(), 50);
    auto p = stocks.get_portfolio(1);
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0].quantity, 50);
}

TEST(Concurrent, ParallelSellsLimited) {
    BankService bank;
    PriceEngine prices;
    StockService stocks(bank, prices);
    auto acc = bank.create_account(1, Currency::USD);
    bank.deposit(1, acc, 1000000);
    stocks.buy(1, "AAPL", 10, acc);

    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 20; i++)
        threads.emplace_back([&] { if (stocks.sell(1, "AAPL", 1, acc)) ok++; });
    for (auto& t : threads) t.join();
    EXPECT_EQ(ok.load(), 10);
    EXPECT_TRUE(stocks.get_portfolio(1).empty());
}
