#include <gtest/gtest.h>
#include "bank_service.hpp"

class BankTest : public ::testing::Test {
protected:
    BankService bank;
    uint64_t uid = 1;
    uint64_t acc;
    void SetUp() override { acc = bank.create_account(uid, Currency::RUB); }
};

TEST_F(BankTest, CreateAccount) {
    auto accs = bank.get_accounts(uid);
    ASSERT_EQ(accs.size(), 1);
    EXPECT_EQ(accs[0].balance, 0.0);
    EXPECT_EQ(accs[0].currency, Currency::RUB);
}

TEST_F(BankTest, Deposit)              { EXPECT_DOUBLE_EQ(*bank.deposit(uid, acc, 1000), 1000.0); }
TEST_F(BankTest, DepositNegative)      { EXPECT_FALSE(bank.deposit(uid, acc, -100)); }
TEST_F(BankTest, DepositZero)          { EXPECT_FALSE(bank.deposit(uid, acc, 0)); }
TEST_F(BankTest, DepositWrongUser)     { EXPECT_FALSE(bank.deposit(999, acc, 100)); }

TEST_F(BankTest, Withdraw) {
    bank.deposit(uid, acc, 1000);
    EXPECT_DOUBLE_EQ(*bank.withdraw(uid, acc, 300), 700.0);
}

TEST_F(BankTest, WithdrawInsufficient) {
    bank.deposit(uid, acc, 100);
    EXPECT_FALSE(bank.withdraw(uid, acc, 200));
}

TEST_F(BankTest, Transfer) {
    auto acc2 = bank.create_account(uid, Currency::RUB);
    bank.deposit(uid, acc, 1000);
    auto r = bank.transfer(uid, acc, acc2, 300);
    ASSERT_TRUE(r);
    EXPECT_DOUBLE_EQ(r->from_balance, 700.0);
    EXPECT_DOUBLE_EQ(r->to_balance, 300.0);
}

TEST_F(BankTest, TransferCrossCurrency) {
    auto usd = bank.create_account(uid, Currency::USD);
    bank.deposit(uid, acc, 9250);
    auto r = bank.transfer(uid, acc, usd, 9250, 1.0 / 92.5);
    ASSERT_TRUE(r);
    EXPECT_NEAR(r->to_balance, 100.0, 0.01);
}

TEST_F(BankTest, TransferInsufficient) {
    auto acc2 = bank.create_account(uid, Currency::RUB);
    bank.deposit(uid, acc, 100);
    EXPECT_FALSE(bank.transfer(uid, acc, acc2, 200));
}

TEST_F(BankTest, TransferToOtherUser) {
    auto other_acc = bank.create_account(42, Currency::RUB);
    bank.deposit(uid, acc, 500);
    auto r = bank.transfer(uid, acc, other_acc, 200);
    ASSERT_TRUE(r);
    EXPECT_DOUBLE_EQ(r->from_balance, 300.0);
    EXPECT_DOUBLE_EQ(r->to_balance, 200.0);
}

TEST_F(BankTest, CloseAccount)       { EXPECT_TRUE(bank.close_account(uid, acc)); }
TEST_F(BankTest, CloseNonZero)       { bank.deposit(uid, acc, 1); EXPECT_FALSE(bank.close_account(uid, acc)); }
TEST_F(BankTest, CloseWrongUser)     { EXPECT_FALSE(bank.close_account(999, acc)); }

TEST_F(BankTest, TransferSelfAccount) {
    bank.deposit(uid, acc, 1000);
    EXPECT_FALSE(bank.transfer(uid, acc, acc, 100));
    EXPECT_DOUBLE_EQ(bank.get_account(acc)->balance, 1000.0);  // unchanged
}

TEST_F(BankTest, History) {
    bank.deposit(uid, acc, 500);
    bank.withdraw(uid, acc, 200);
    auto h = bank.get_history(acc);
    ASSERT_EQ(h.size(), 2);
    EXPECT_EQ(h[0].type, OpType::Deposit);
    EXPECT_EQ(h[1].type, OpType::Withdraw);
    EXPECT_DOUBLE_EQ(h[0].balance_after, 500.0);
    EXPECT_DOUBLE_EQ(h[1].balance_after, 300.0);
}
