#include "local_cache.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>

namespace {

int failures = 0;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "[FAIL] " << message << '\n';
        return;
    }

    std::cout << "[PASS] " << message << '\n';
}

void TestBuildPermissionCacheKey() {
    const std::string key = BuildPermissionCacheKey("demo_app", "user_001", 7);

    Expect(key == "demo_app:user_001:7", "BuildPermissionCacheKey formats app/user/version");
}

void TestPutAndGet() {
    LocalCache<std::unordered_set<std::string>> cache;

    cache.Put("demo_app:user_001:1", {"document:read", "document:edit"}, 5);

    std::unordered_set<std::string> value;
    const bool found = cache.Get("demo_app:user_001:1", value);

    Expect(found, "Get returns true for an existing non-expired key");
    Expect(value.count("document:read") == 1, "cached value contains document:read");
    Expect(value.count("document:edit") == 1, "cached value contains document:edit");
    Expect(cache.Size() == 1, "cache size is 1 after Put");

    const auto stats = cache.GetStats();
    Expect(stats.put_count == 1, "put_count is incremented");
    Expect(stats.hit_count == 1, "hit_count is incremented");
    Expect(stats.miss_count == 0, "miss_count stays 0 after a hit");
}

void TestMissAndExpiration() {
    LocalCache<std::string> cache;

    std::string value;
    Expect(!cache.Get("missing", value), "Get returns false for a missing key");

    cache.Put("short_lived", "value", 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    Expect(!cache.Get("short_lived", value), "expired key is treated as a miss");
    Expect(cache.Size() == 0, "expired key is removed from cache");

    const auto stats = cache.GetStats();
    Expect(stats.miss_count == 2, "miss_count includes missing and expired keys");
    Expect(stats.invalidate_count == 1, "invalidate_count increments when an expired key is removed");
}

void TestInvalidateOperations() {
    LocalCache<int> cache;

    cache.Put("demo:user_001:1", 1, 5);
    cache.Put("demo:user_002:1", 2, 5);
    cache.Put("other:user_003:1", 3, 5);

    Expect(cache.Invalidate("demo:user_001:1"), "Invalidate returns true for an existing key");
    Expect(!cache.Invalidate("demo:user_404:1"), "Invalidate returns false for a missing key");
    Expect(cache.Size() == 2, "cache size decreases after Invalidate");

    const std::size_t removed = cache.InvalidatePrefix("demo:");
    Expect(removed == 1, "InvalidatePrefix removes matching keys");
    Expect(cache.Size() == 1, "non-matching keys remain after InvalidatePrefix");

    cache.Clear();
    Expect(cache.Size() == 0, "Clear removes all keys");

    const auto stats = cache.GetStats();
    Expect(stats.invalidate_count == 3, "invalidate_count includes key, prefix, and clear removals");
}

void TestMaxEntriesEviction() {
    LocalCache<int> cache(2);

    cache.Put("first", 1, 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    cache.Put("second", 2, 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    cache.Put("third", 3, 5);

    int value = 0;
    Expect(cache.Size() == 2, "cache respects max_entries after Put");
    Expect(!cache.Get("first", value), "oldest entry is evicted when max_entries is exceeded");
    Expect(cache.Get("second", value), "newer entry remains after eviction");
    Expect(cache.Get("third", value), "latest entry remains after eviction");

    const auto stats = cache.GetStats();
    Expect(stats.eviction_count == 1, "eviction_count increments when max_entries evicts");
}

}  // namespace

int main() {
    TestBuildPermissionCacheKey();
    TestPutAndGet();
    TestMissAndExpiration();
    TestInvalidateOperations();
    TestMaxEntriesEviction();

    if (failures > 0) {
        std::cerr << failures << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "All local cache tests passed.\n";
    return 0;
}
