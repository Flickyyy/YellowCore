#include "price_engine.hpp"
#include <random>

PriceEngine::PriceEngine() {
    quotes_ = {
        {"AAPL", 178.50}, {"GOOGL", 140.20}, {"TSLA", 245.00}, {"AMZN", 185.60},
        {"MSFT", 415.30}, {"NFLX", 620.00}, {"META", 510.40}, {"NVDA", 790.00}
    };
    usd_rates_ = {
        {Currency::USD, 1.0},
        {Currency::RUB, 92.5},
        {Currency::EUR, 0.92}
    };
}

PriceEngine::~PriceEngine() { stop(); }

void PriceEngine::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&PriceEngine::run, this);
}

void PriceEngine::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_one();
    if (thread_.joinable()) thread_.join();
}

double PriceEngine::get_quote(const std::string& ticker) const {
    std::shared_lock lock(mu_);
    auto it = quotes_.find(ticker);
    return it != quotes_.end() ? it->second : 0.0;
}

std::unordered_map<std::string, double> PriceEngine::get_all_quotes() const {
    std::shared_lock lock(mu_);
    return quotes_;
}

double PriceEngine::get_rate(Currency from, Currency to) const {
    if (from == to) return 1.0;
    std::shared_lock lock(mu_);
    return usd_rates_.at(to) / usd_rates_.at(from);
}

void PriceEngine::run() {
    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<> stock_pct(-0.03, 0.03);
    std::uniform_real_distribution<> fx_pct(-0.005, 0.005);

    while (running_) {
        {
            std::unique_lock lock(cv_mu_);
            cv_.wait_for(lock, std::chrono::seconds(2), [this] { return !running_.load(); });
        }
        if (!running_) break;

        std::unique_lock lock(mu_);
        for (auto& [_, price] : quotes_)
            price *= (1.0 + stock_pct(rng));
        for (auto& [cur, rate] : usd_rates_)
            if (cur != Currency::USD)
                rate *= (1.0 + fx_pct(rng));
    }
}
