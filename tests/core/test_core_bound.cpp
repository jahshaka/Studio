// core.bound - iris::bound IS qBound, BIT FOR BIT (NOQT-CORE-1 stage 1; core/math/scalar.h): over a seeded set of (lo, v, hi)
// with NaN, +-inf and +-0 among them (hi < lo never: qBound's own precondition), iris::bound(lo, v, hi) has the same bits as qBound(lo, v, hi), floats and doubles.
// Links QtCore for the reference only (like math.parity).
#include "core/math/scalar.h"

#include <QtGlobal>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
static long checks = 0, special = 0;

template <typename T>
static bool same(T a, T b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }

template <typename T>
static void run(const char *type)
{
    const T nan = std::numeric_limits<T>::quiet_NaN(), inf = std::numeric_limits<T>::infinity();
    std::vector<T> pool = { T(0), -T(0), nan, inf, -inf, T(1), T(-1), T(0.5), std::numeric_limits<T>::denorm_min(),
                            std::numeric_limits<T>::max(), std::numeric_limits<T>::lowest() };
    std::mt19937_64 rng(0xB0A7D5EEDull);
    std::uniform_real_distribution<double> d(-4.0, 4.0);
    for (int i = 0; i < 200; ++i) pool.push_back(T(d(rng)));
    std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
    for (int i = 0; i < 200000; ++i) {
        // a third of the draws from the special values alone (the edges are the point)
        const bool sp = i % 3 == 0;
        auto draw = [&] { return sp ? pool[pick(rng) % 11] : pool[pick(rng)]; };
        const T lo = draw(), v = draw(), hi = draw();
        if (hi < lo) continue;               // qBound's precondition (it asserts !(max < min)); NaN bounds pass it
        const T want = qBound(lo, v, hi), got = iris::bound(lo, v, hi);
        ++checks;
        if (std::isnan(lo) || std::isnan(v) || std::isnan(hi) || (v == 0 && std::signbit(v)) || std::isinf(v)) ++special;
        if (!same(want, got) && ++failures <= 20)
            std::printf("FAIL %s: bound(%g, %g, %g) = %g, qBound = %g\n", type, double(lo), double(v), double(hi), double(got),
                        double(want));
    }
}

int main()
{
    run<float>("float");
    run<double>("double");
    std::printf("%ld triples, %ld with a NaN, an infinity or -0.0; %d failures\n", checks, special, failures);
    if (special == 0) { std::printf("FAIL no special case drawn\n"); ++failures; }
    std::printf("core.bound: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
