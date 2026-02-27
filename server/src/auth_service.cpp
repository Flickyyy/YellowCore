#include "auth_service.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <functional>

// TODO: заменить на SHA-256 (OpenSSL / Boost) при переходе в продакшн.
// std::hash достаточен для демонстрации и прохождения тестов.
std::string AuthService::hash(const std::string& s) {
    auto h = std::hash<std::string>{}(s);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << h;
    return oss.str();
}

std::string AuthService::gen_token() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << rng() << rng();
    return oss.str();
}

std::optional<uint64_t> AuthService::register_user(const std::string& username, const std::string& password) {
    std::lock_guard lock(mu_);
    if (users_.count(username)) return std::nullopt;
    User u{next_id_++, username, hash(password)};
    users_[username] = u;
    return u.id;
}

std::optional<std::string> AuthService::login(const std::string& username, const std::string& password) {
    std::lock_guard lock(mu_);
    auto it = users_.find(username);
    if (it == users_.end() || it->second.password_hash != hash(password))
        return std::nullopt;
    auto token = gen_token();
    tokens_[token] = it->second.id;
    return token;
}

bool AuthService::logout(const std::string& token) {
    std::lock_guard lock(mu_);
    return tokens_.erase(token) > 0;
}

std::optional<uint64_t> AuthService::validate(const std::string& token) const {
    std::lock_guard lock(mu_);
    auto it = tokens_.find(token);
    if (it == tokens_.end()) return std::nullopt;
    return it->second;
}
