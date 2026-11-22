// core.text - THE TEXT HELPERS' LAWS (NOQT-CORE-1 stage 1): UTF-8 validation refuses an overlong form, a lone surrogate and a
// truncated sequence and accepts 4-byte forms; ASCII folding keeps every other byte; trim/split/join identities; a double's
// shortest text parses back to the SAME BITS over 100,000 seeded doubles and the edges (+-0, the denormal edges, 2^53 +- 1,
// +-1e308, +-inf), also under a decimal-COMMA locale (de_DE.UTF-8, else en_DK.UTF-8: a locale-bound parse fails there); parseInt
// refuses trailing text and overflow. A missing locale prints SKIPPED with its name; the C-locale pass always runs.
#include "irisgl/core/text.h"

#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
static void check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok && ++failures <= 40) std::printf("FAIL %s\n", what.c_str());
}

static std::uint64_t bitsOf(double v) { std::uint64_t b; std::memcpy(&b, &v, sizeof b); return b; }

// every non-NaN double of the set parses back from its shortest text to the same bits; the count of round trips made
static long roundTrips(const std::vector<double> &set, const char *pass)
{
    long n = 0;
    for (double v : set) {
        const std::string s = iris::text::shortest(v);
        const auto back = iris::text::parseDouble(s);
        ++n;
        if (!back || bitsOf(*back) != bitsOf(v)) {
            check(false, std::string(pass) + ": shortest(" + s + ") did not parse back to the same bits");
            if (failures > 40) return n;
        }
    }
    return n;
}

int main()
{
    using namespace iris::text;
    // ---- UTF-8
    struct U { const char *bytes; std::size_t n; bool valid; const char *what; };
    const U utf[] = {
        { "plain ascii", 11, true, "ASCII" },
        { "W\xC3\xBCrfel \xE2\x98\x83", 11, true, "2- and 3-byte forms" },
        { "\xF0\x9F\x98\x80", 4, true, "a 4-byte form (U+1F600)" },
        { "\xF4\x8F\xBF\xBF", 4, true, "U+10FFFF" },
        { "\xC0\xAF", 2, false, "an overlong '/' (C0 AF)" },
        { "\xE0\x80\xAF", 3, false, "an overlong 3-byte form" },
        { "\xF0\x80\x80\xAF", 4, false, "an overlong 4-byte form" },
        { "\xED\xA0\x80", 3, false, "a lone surrogate (U+D800)" },
        { "\xED\xBF\xBF", 3, false, "a lone surrogate (U+DFFF)" },
        { "\xF4\x90\x80\x80", 4, false, "above U+10FFFF" },
        { "\xE2\x98", 2, false, "a truncated 3-byte form" },
        { "\xF0\x9F\x98", 3, false, "a truncated 4-byte form" },
        { "\x80", 1, false, "a continuation byte first" },
        { "\xFF", 1, false, "0xFF" },
    };
    for (const U &u : utf)
        check(validUtf8(std::string_view(u.bytes, u.n)) == u.valid, std::string("validUtf8: ") + u.what);
    // ---- ASCII folding keeps every other byte
    check(asciiLower("MeSH.OBJ W\xC3\x9C") == "mesh.obj w\xC3\x9C", "asciiLower folds ASCII and keeps the UTF-8 bytes");
    check(asciiUpper("a\xC3\xBC") == "A\xC3\xBC", "asciiUpper keeps the UTF-8 bytes");
    check(iequalsAscii("Scene.JSON", "scene.json") && !iequalsAscii("a", "ab") && !iequalsAscii("\xC3\xBC", "\xC3\x9C"),
          "iequalsAscii: ASCII only");
    check(startsWith("abc", "ab") && !startsWith("a", "ab") && endsWith("x.png", ".png") && !endsWith("png", "x.png"),
          "startsWith / endsWith");
    // ---- trim / split / join identities
    check(trim(" \t a b \r\n") == "a b" && trim("   ").empty() && trim("") == "", "trim");
    for (const char *s : { "", "a", "a,,b", ",a,", "a,b,c" })
        check(join(split(s, ','), ",") == s, std::string("join(split(s)) == s for \"") + s + "\"");
    check(split("a,,b", ',').size() == 3 && split("", ',').size() == 1, "split keeps empty pieces");
    // ---- integers
    check(parseInt("42") == 42 && parseInt("-9223372036854775808") == std::numeric_limits<std::int64_t>::min(), "parseInt");
    check(!parseInt("42x") && !parseInt(" 42") && !parseInt("") && !parseInt("+1") && !parseInt("9223372036854775808"),
          "parseInt refuses trailing text, space, empty, '+' and overflow");
    check(!parseDouble("1.5x") && !parseDouble(" 1") && !parseDouble("") && !parseDouble("1e400"),
          "parseDouble refuses trailing text, space, empty and overflow");
    check(cat("n=", 3, " x=", 0.1, ' ', true) == "n=3 x=0.1 true", "cat: " + cat("n=", 3, " x=", 0.1, ' ', true));
    check(fixed(1.0 / 3.0, 3) == "0.333" && fixed(2.5, 0) == "2", "fixed: " + fixed(1.0 / 3.0, 3) + " " + fixed(2.5, 0));
    // ---- the round trip: 100,000 seeded doubles (random BITS, NaN excluded) and the edges
    std::vector<double> set;
    std::mt19937_64 rng(0x1A2B3C4D5E6F7788ull);
    while (set.size() < 100000) {
        const std::uint64_t b = rng();
        double v;
        std::memcpy(&v, &b, sizeof v);
        if (!std::isnan(v)) set.push_back(v);
    }
    const double edges[] = { 0.0, -0.0, std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
                             std::numeric_limits<double>::min(), std::nextafter(std::numeric_limits<double>::min(), 0.0),
                             9007199254740992.0 - 1, 9007199254740992.0, 9007199254740992.0 + 2, 1e308, -1e308,
                             std::numeric_limits<double>::max(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(), 0.1, 1.0 / 3.0 };
    set.insert(set.end(), std::begin(edges), std::end(edges));
    const long cPass = roundTrips(set, "C locale");
    check(cPass == 100000 + long(sizeof edges / sizeof edges[0]), "the C-locale round trips counted " + std::to_string(cPass));
    // THE COMMA LOCALE: de_DE.UTF-8 (the spec's), else en_DK.UTF-8 (a decimal comma too, installed where de_DE is not) -
    // taken only when its decimal point IS a comma, so the pass proves what it claims
    const char *de = nullptr;
    for (const char *name : { "de_DE.UTF-8", "de_DE.utf8", "en_DK.UTF-8", "en_DK.utf8" }) {
        if (std::setlocale(LC_ALL, name) && std::strcmp(std::localeconv()->decimal_point, ",") == 0) { de = name; break; }
        std::setlocale(LC_ALL, "C");
    }
    if (de) {
        const long dPass = roundTrips(set, de);
        check(dPass == cPass, std::string(de) + ": round trips counted " + std::to_string(dPass));
        check(shortest(0.5) == "0.5" && parseDouble("0.5") == 0.5, std::string(de) + ": a point, never the locale's comma");
        std::setlocale(LC_ALL, "C");
        std::printf("round trips: %ld in the C locale, %ld under %s (decimal comma)\n", cPass, dPass, de);
    } else {
        std::printf("SKIPPED the comma-locale pass: none of de_DE.UTF-8, en_DK.UTF-8 is installed here (the C-locale pass ran: "
                    "%ld round trips)\n", cPass);
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    std::printf("core.text: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
