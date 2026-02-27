#include <gtest/gtest.h>
#include "stock_service.hpp"

class StockTest : public ::testing::Test {
protected:
    BankService bank;
    PriceEngine prices;
    std::unique_ptr<StockService> stocks;
    uint64_t uid = 1, acc;

    void SetUp() override {
        stocks = std::make_unique<StockService>(bank, prices);
        acc = bank.create_account(uid, Currency::USD);
        bank.deposit(uid, acc, 10000);
    }
};

TEST_F(StockTest, Buy) {
    auto r = stocks->buy(uid, "AAPL", 2, acc);
    ASSERT_TRUE(r);
    EXPECT_GT(r->price, 0);
    EXPECT_GT(r->total_cost, 0);
    auto p = stocks->get_portfolio(uid);
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0].quantity, 2);
}

TEST_F(StockTest, BuyAndSell) {
    stocks->buy(uid, "AAPL", 5, acc);
    auto r = stocks->sell(uid, "AAPL", 3, acc);
    ASSERT_TRUE(r);
    auto p = stocks->get_portfolio(uid);
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0].quantity, 2);
}

TEST_F(StockTest, SellAll) {
    stocks->buy(uid, "AAPL", 5, acc);
    stocks->sell(uid, "AAPL", 5, acc);
    EXPECT_TRUE(stocks->get_portfolio(uid).empty());
}

TEST_F(StockTest, InsufficientFunds) {
    EXPECT_FALSE(stocks->buy(uid, "NVDA", 100, acc));  // ~$79k > $10k
}

TEST_F(StockTest, SellMoreThanOwned) {
    stocks->buy(uid, "AAPL", 2, acc);
    EXPECT_FALSE(stocks->sell(uid, "AAPL", 5, acc));
    EXPECT_EQ(stocks->get_portfolio(uid)[0].quantity, 2);  // unchanged
}

TEST_F(StockTest, InvalidTicker)     { EXPECT_FALSE(stocks->buy(uid, "FAKE", 1, acc)); }
TEST_F(StockTest, SellWithoutOwning) { EXPECT_FALSE(stocks->sell(uid, "AAPL", 1, acc)); }
TEST_F(StockTest, ZeroQuantity)      { EXPECT_FALSE(stocks->buy(uid, "AAPL", 0, acc)); }
TEST_F(StockTest, NegativeQuantity)  { EXPECT_FALSE(stocks->buy(uid, "AAPL", -1, acc)); }

TEST_F(StockTest, TradeHistory) {
    stocks->buy(uid, "AAPL", 2, acc);
    stocks->sell(uid, "AAPL", 1, acc);
    auto t = stocks->get_trades(uid);
    ASSERT_EQ(t.size(), 2);
    EXPECT_TRUE(t[0].is_buy);
    EXPECT_FALSE(t[1].is_buy);
}

TEST_F(StockTest, BuyWithRubAccount) {
    auto rub = bank.create_account(uid, Currency::RUB);
    bank.deposit(uid, rub, 100000);
    auto r = stocks->buy(uid, "AAPL", 1, rub);
    ASSERT_TRUE(r);
    EXPECT_GT(r->total_cost, 10000);  // ~178.5 * 92.5 ≈ 16,511 RUB
}

TEST_F(StockTest, AvgPriceCalculation) {
    stocks->buy(uid, "AAPL", 2, acc);
    stocks->buy(uid, "AAPL", 2, acc);
    auto p = stocks->get_portfolio(uid);
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0].quantity, 4);
    EXPECT_NEAR(p[0].avg_price, 178.50, 0.01);  // price engine not running
}

TEST_F(StockTest, WrongUser) {
    EXPECT_FALSE(stocks->buy(999, "AAPL", 1, acc));
}
