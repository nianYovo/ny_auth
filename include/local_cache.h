#ifndef NY_AUTH_LOCAL_CACHE_H
#define NY_AUTH_LOCAL_CACHE_H

#include <chrono>

#include <cstddef>

#include <mutex>

#include <string>

#include <unordered_map>

inline std::string BuildPermissionCacheKey(const std::string& app_code,const std::string& user_id,int policy_version) {
    return app_code + ":" + user_id + ":" + std::to_string(policy_version);
}

template <typename T>
class LocalCache {
public:

    using Clock = std::chrono::steady_clock;

    struct Entry {

        T value;

        Clock::time_point expire_at;

        Clock::time_point inserted_at;
    };

    struct CacheStats {

        std::size_t hit_count = 0;

        std::size_t miss_count = 0;

        std::size_t put_count = 0;

        std::size_t invalidate_count = 0;

        std::size_t eviction_count = 0;
    };

    explicit LocalCache(std::size_t max_entries = 0) : max_entries_(max_entries) {}

    void SetMaxEntries(std::size_t max_entries) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_entries_ = max_entries;
        evictIfNeededLocked(Clock::now());
    }

    void Put(const std::string& key,const T& value, int ttl_seconds) {

        std::lock_guard<std::mutex> lock(mutex_);

        const auto now = Clock::now();

        cache_[key] = Entry{value, now + std::chrono::seconds(ttl_seconds), now};

        ++stats_.put_count;

        evictIfNeededLocked(now);
    }

    bool Get(const std::string& key,T& value) {

        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_.find(key);

        if(it == cache_.end()) {
            
            ++stats_.miss_count;

            return false;
        }

        if (Clock::now() > it->second.expire_at) {

            cache_.erase(it);

            ++stats_.miss_count;
            ++stats_.invalidate_count;

            return false;
        }

        value = it->second.value;

        ++stats_.hit_count;

        return true;
    }

    bool Invalidate(const std::string& key) {

        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t erased = cache_.erase(key);

        if(erased > 0) {
            
            ++stats_.invalidate_count;

            return true;
        }

        return false;
    }

    std::size_t InvalidatePrefix(const std::string& prefix) {

        std::lock_guard<std::mutex> lock(mutex_);

        std::size_t removed = 0;

        for (auto it = cache_.begin(); it != cache_.end();) {

            if(it->first.compare(0, prefix.size(), prefix) == 0) {
                
                it = cache_.erase(it);

                ++removed;
            }
            else {

                ++it;
            }
        }

        stats_.invalidate_count += removed;

        return removed;
    }

    void Clear() {

        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t current_size = cache_.size();

        cache_.clear();

        stats_.invalidate_count += current_size;
    }

    std::size_t Size() const {

        std::lock_guard<std::mutex> lock(mutex_);

        return cache_.size();
    }

    CacheStats GetStats() const {
        
        std::lock_guard<std::mutex> lock(mutex_);

        return stats_;
    }

private:

    void evictIfNeededLocked(Clock::time_point now) {
        if (cache_.empty()) {
            return;
        }

        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now > it->second.expire_at) {
                it = cache_.erase(it);
                ++stats_.invalidate_count;
            } else {
                ++it;
            }
        }

        if (max_entries_ == 0) {
            return;
        }

        while (cache_.size() > max_entries_) {
            auto oldest_it = cache_.end();
            auto oldest_time = Clock::time_point::max();

            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.inserted_at < oldest_time) {
                    oldest_time = it->second.inserted_at;
                    oldest_it = it;
                }
            }

            if (oldest_it == cache_.end()) {
                return;
            }

            cache_.erase(oldest_it);
            ++stats_.eviction_count;
            ++stats_.invalidate_count;
        }
    }

    std::unordered_map<std::string, Entry> cache_;

    CacheStats stats_;

    std::size_t max_entries_ = 0;

    mutable std::mutex mutex_;
};

#endif
