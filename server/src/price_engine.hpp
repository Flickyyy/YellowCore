#pragma once
#include "models.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

class PriceEngine {
public:
    PriceEngine();
    ~PriceEngine();

    void start();
    void stop();
    bool is_running() const { return running_.load(); }

    double get_quote(const std::string& ticker) const;
    std::unordered_map<std::string, double> get_all_quotes() const;
    double get_rate(Currency from, Currency to) const;

private:
    void run();

    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, double> quotes_;
    std::unordered_map<Currency, double> usd_rates_;  // 1 USD = X units of currency

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex cv_mu_;
    std::condition_variable cv_;
};
