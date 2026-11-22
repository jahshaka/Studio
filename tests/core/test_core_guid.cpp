// core.guid - THE ONE GUID's FORM (NOQT-CORE-1 stage 1; core/guid.h): 36 characters, lowercase hex, dashes at 8/13/18/23,
// version 4, variant 8-b; 100,000 drawn are unique (across 4 threads). The stored keys it must match are checked by
// tests/core/guid_library.py over the seeded template library (the same form, by the same row).
#include "core/guid.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

static bool form(const std::string &g)
{
    if (g.size() != 36) return false;
    for (int i = 0; i < 36; ++i) {
        const char c = g[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; continue; }
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return g[14] == '4' && (g[19] == '8' || g[19] == '9' || g[19] == 'a' || g[19] == 'b');
}

int main()
{
    int failures = 0;
    std::unordered_set<std::string> seen;
    std::mutex m;
    long bad = 0, dup = 0;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&] {
            for (int i = 0; i < 25000; ++i) {
                const std::string g = iris::newGuid();
                std::lock_guard<std::mutex> lk(m);
                if (!form(g) && ++bad <= 5) std::printf("FAIL not the form: %s\n", g.c_str());
                if (!seen.insert(g).second) ++dup;
            }
        });
    for (auto &t : ts) t.join();
    if (bad) ++failures;
    if (dup || seen.size() != 100000) { std::printf("FAIL %ld duplicates in 100000\n", dup); ++failures; }
    const std::string one = iris::newGuid();
    std::printf("%zu unique guids, %ld not of the form; e.g. %s\n", seen.size(), bad, one.c_str());
    std::printf("core.guid: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
