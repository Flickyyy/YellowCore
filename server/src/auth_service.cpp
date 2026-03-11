#include "auth_service.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <functional>

// TODO: заменить на SHA-256 или лучше, с солью
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
    User u{next_id_++, username, hash(password)};
    if (!users_.try_insert(username, u)) return std::nullopt;
    return u.id;
}

std::optional<std::string> AuthService::login(const std::string& username, const std::string& password) {
    auto user = users_.get(username);
    if (!user || user->password_hash != hash(password))
        return std::nullopt;
    auto token = gen_token();
    tokens_.put(token, user->id);
    return token;
}

bool AuthService::logout(const std::string& token) {
    return tokens_.erase(token);
}

std::optional<uint64_t> AuthService::validate(const std::string& token) const {
    return tokens_.get(token);
}
