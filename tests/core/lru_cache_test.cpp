#include "core/lru_cache.hpp"

#include <catch2/catch_test_macros.hpp>
#include <random>
#include <string>

using streetpano::core::LruCache;

TEST_CASE("items can be stored and found") {
    LruCache<int, std::string> cache(100);
    REQUIRE(cache.put(1, "one", 10));
    REQUIRE(cache.get(1) == "one");
    REQUIRE(cache.contains(1));
    REQUIRE_FALSE(cache.get(2).has_value());
    REQUIRE(cache.usedBytes() == 10);
}

TEST_CASE("the oldest items are dropped when the budget is exceeded") {
    LruCache<int, int> cache(30);
    cache.put(1, 1, 10);
    cache.put(2, 2, 10);
    cache.put(3, 3, 10);
    cache.put(4, 4, 10);
    REQUIRE_FALSE(cache.contains(1));
    REQUIRE(cache.contains(2));
    REQUIRE(cache.usedBytes() == 30);
}

TEST_CASE("reading an item keeps it from being dropped") {
    LruCache<int, int> cache(30);
    cache.put(1, 1, 10);
    cache.put(2, 2, 10);
    cache.put(3, 3, 10);
    REQUIRE(cache.get(1).has_value());
    cache.put(4, 4, 10);
    REQUIRE(cache.contains(1));
    REQUIRE_FALSE(cache.contains(2));
}

TEST_CASE("replacing an item updates its size") {
    LruCache<int, int> cache(100);
    cache.put(1, 1, 60);
    cache.put(1, 2, 20);
    REQUIRE(cache.get(1) == 2);
    REQUIRE(cache.usedBytes() == 20);
    REQUIRE(cache.size() == 1);
}

TEST_CASE("an item larger than the budget is rejected") {
    LruCache<int, int> cache(10);
    cache.put(1, 1, 5);
    REQUIRE_FALSE(cache.put(2, 2, 11));
    REQUIRE_FALSE(cache.contains(2));
    REQUIRE(cache.contains(1));
}

TEST_CASE("rejecting a replacement removes the old copy") {
    LruCache<int, int> cache(10);
    cache.put(1, 1, 5);
    REQUIRE_FALSE(cache.put(1, 2, 11));
    REQUIRE_FALSE(cache.contains(1));
    REQUIRE(cache.usedBytes() == 0);
}

TEST_CASE("one large item can push out several small ones") {
    LruCache<int, int> cache(30);
    cache.put(1, 1, 10);
    cache.put(2, 2, 10);
    cache.put(3, 3, 25);
    REQUIRE(cache.size() == 1);
    REQUIRE(cache.contains(3));
}

TEST_CASE("items can be erased and cleared") {
    LruCache<int, int> cache(100);
    cache.put(1, 1, 10);
    cache.put(2, 2, 10);
    cache.erase(1);
    cache.erase(99);
    REQUIRE(cache.usedBytes() == 10);
    cache.clear();
    REQUIRE(cache.usedBytes() == 0);
    REQUIRE(cache.size() == 0);
    REQUIRE_FALSE(cache.contains(2));
}

TEST_CASE("the budget holds under random use") {
    constexpr std::size_t kBudget = 1000;
    LruCache<int, int> cache(kBudget);
    std::mt19937 random(12345);
    std::uniform_int_distribution<int> key(0, 50);
    std::uniform_int_distribution<int> action(0, 9);
    std::uniform_int_distribution<std::size_t> bytes(1, 400);
    for (int step = 0; step < 100000; ++step) {
        const int choice = action(random);
        if (choice < 6) {
            cache.put(key(random), step, bytes(random));
        } else if (choice < 9) {
            static_cast<void>(cache.get(key(random)));
        } else {
            cache.erase(key(random));
        }
        REQUIRE(cache.usedBytes() <= kBudget);
    }
}
