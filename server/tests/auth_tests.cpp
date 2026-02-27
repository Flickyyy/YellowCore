#include <gtest/gtest.h>
#include "auth_service.hpp"

// --- Models helpers ---
TEST(Models, CurrencyRoundTrip) {
    EXPECT_EQ(currency_from_string(to_string(Currency::RUB)), Currency::RUB);
    EXPECT_EQ(currency_from_string(to_string(Currency::USD)), Currency::USD);
    EXPECT_EQ(currency_from_string(to_string(Currency::EUR)), Currency::EUR);
    EXPECT_FALSE(currency_from_string("BTC"));
}

TEST(Models, OpTypeRoundTrip) {
    EXPECT_EQ(optype_from_string(to_string(OpType::Deposit)), OpType::Deposit);
    EXPECT_EQ(optype_from_string(to_string(OpType::Withdraw)), OpType::Withdraw);
    EXPECT_EQ(optype_from_string(to_string(OpType::TransferIn)), OpType::TransferIn);
    EXPECT_EQ(optype_from_string(to_string(OpType::TransferOut)), OpType::TransferOut);
    EXPECT_EQ(optype_from_string(to_string(OpType::BuyStock)), OpType::BuyStock);
    EXPECT_EQ(optype_from_string(to_string(OpType::SellStock)), OpType::SellStock);
    EXPECT_FALSE(optype_from_string("unknown"));
}

// --- Auth ---

TEST(Auth, RegisterAndLogin) {
    AuthService auth;
    auto id = auth.register_user("alice", "pass123");
    ASSERT_TRUE(id);
    auto token = auth.login("alice", "pass123");
    ASSERT_TRUE(token);
    auto uid = auth.validate(*token);
    ASSERT_TRUE(uid);
    EXPECT_EQ(*uid, *id);
}

TEST(Auth, DuplicateUsername) {
    AuthService auth;
    ASSERT_TRUE(auth.register_user("alice", "pass"));
    ASSERT_FALSE(auth.register_user("alice", "other"));
}

TEST(Auth, WrongPassword) {
    AuthService auth;
    auth.register_user("alice", "pass");
    ASSERT_FALSE(auth.login("alice", "wrong"));
}

TEST(Auth, Logout) {
    AuthService auth;
    auth.register_user("alice", "pass");
    auto token = auth.login("alice", "pass");
    ASSERT_TRUE(auth.validate(*token));
    ASSERT_TRUE(auth.logout(*token));
    ASSERT_FALSE(auth.validate(*token));
}

TEST(Auth, InvalidToken) {
    AuthService auth;
    ASSERT_FALSE(auth.validate("bogus"));
}

TEST(Auth, UnknownUser) {
    AuthService auth;
    ASSERT_FALSE(auth.login("nobody", "pass"));
}
