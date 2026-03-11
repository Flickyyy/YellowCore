#pragma once
#include "models.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <atomic>

class IAuthService {
public:
    virtual ~IAuthService() = default;
    virtual std::optional<uint64_t> register_user(const std::string& username, const std::string& password) = 0;
    virtual std::optional<std::string> login(const std::string& username, const std::string& password) = 0;
    virtual bool logout(const std::string& token) = 0;
    virtual std::optional<uint64_t> validate(const std::string& token) const = 0;
};

class AuthService : public IAuthService {
public:
    std::optional<uint64_t> register_user(const std::string& username, const std::string& password) override;
    std::optional<std::string> login(const std::string& username, const std::string& password) override;
    bool logout(const std::string& token) override;
    std::optional<uint64_t> validate(const std::string& token) const override;

private:
    static std::string hash(const std::string& s);
    static std::string gen_token();

    mutable std::shared_mutex users_mu_;
    mutable std::shared_mutex tokens_mu_;
    std::unordered_map<std::string, User> users_;
    std::unordered_map<std::string, uint64_t> tokens_;
    std::atomic<uint64_t> next_id_{1};
};
