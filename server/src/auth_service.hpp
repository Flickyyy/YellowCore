#pragma once
#include "models.hpp"
#include <unordered_map>
#include <mutex>
#include <optional>
#include <atomic>

class AuthService {
public:
    std::optional<uint64_t> register_user(const std::string& username, const std::string& password);
    std::optional<std::string> login(const std::string& username, const std::string& password);
    bool logout(const std::string& token);
    std::optional<uint64_t> validate(const std::string& token) const;

private:
    static std::string hash(const std::string& s);
    static std::string gen_token();

    mutable std::mutex mu_;
    std::unordered_map<std::string, User> users_;
    std::unordered_map<std::string, uint64_t> tokens_;
    std::atomic<uint64_t> next_id_{1};
};
