/**
 * @file
 * @brief Deterministic random helpers for reproducible native and browser levels.
 *
 * @details Standard distributions and shuffle may differ between C++ libraries.
 * Preserve draw order when refactoring seeded generation.
 */
#pragma once

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace bc {

class Rng {
public:
    explicit Rng(std::uint32_t seed) : gen_(seed) {}

    std::uint32_t next() { return static_cast<std::uint32_t>(gen_()); }
    std::uint64_t next64() { return (std::uint64_t{next()} << 32) | next(); }

    /** @brief Draw an integer from the inclusive interval [lo, hi]. */
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1));
    }

    bool chance(double p) { return static_cast<double>(next()) < p * 4294967296.0; }

    template <class T>
    void shuffle(std::vector<T>& v) {
        for (int i = static_cast<int>(v.size()) - 1; i > 0; --i) std::swap(v[i], v[range(0, i)]);
    }

private:
    std::mt19937 gen_;
};

}  // namespace bc
