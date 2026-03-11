#pragma once
#include <array>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <optional>
#include <functional>

template<typename K, typename V, size_t Shards = 16>
class ConcurrentMap {
    static_assert(Shards > 0, "Need at least one shard");
public:
    std::optional<V> get(const K& key) const {
        auto& s = shard_for(key);
        std::shared_lock lk(s.mu);
        auto it = s.data.find(key);
        return it != s.data.end() ? std::optional<V>(it->second) : std::nullopt;
    }

    bool try_insert(const K& key, const V& value) {
        auto& s = shard_for(key);
        std::unique_lock lk(s.mu);
        return s.data.emplace(key, value).second;
    }

    void put(const K& key, const V& value) {
        auto& s = shard_for(key);
        std::unique_lock lk(s.mu);
        s.data.insert_or_assign(key, value);
    }

    bool erase(const K& key) {
        auto& s = shard_for(key);
        std::unique_lock lk(s.mu);
        return s.data.erase(key) > 0;
    }

    template<typename Factory>
    V get_or_create(const K& key, Factory&& factory) {
        auto& s = shard_for(key);
        {
            std::shared_lock lk(s.mu);
            auto it = s.data.find(key);
            if (it != s.data.end()) return it->second;
        }
        {
            std::unique_lock lk(s.mu);
            auto [it, inserted] = s.data.try_emplace(key);
            if (inserted) it->second = factory();
            return it->second;
        }
    }

private:
    struct Shard {
        mutable std::shared_mutex mu;
        std::unordered_map<K, V> data;
    };

    mutable std::array<Shard, Shards> shards_{};

    Shard& shard_for(const K& key) const {
        return shards_[std::hash<K>{}(key) % Shards];
    }
};
