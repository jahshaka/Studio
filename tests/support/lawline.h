#pragma once
// THE LAW LINE (HARNESS-3 (b), the lead's §3613: a record keeps its row's law numbers). A row prints each number its law
// judges as ONE line,
//     LAW <name> <value> <unit> [<op> <bar>]
// and scripts/gate_runlog.py keeps them in the row's record (`laws`: {name: {value, unit, op, bar}}), green or red, so a
// regression's range reads from testing/runs by the NUMBER, not only by the first FAIL. <name> one token ([A-Za-z0-9_.:+-],
// unique in the row: a tier or a pose as its prefix), <value> a number (nan / inf allowed), <unit> one token (codes, %, %R,
// q, ms, frames, px, x), <op> one of <= < >= > == and <bar> the law's bound in the same unit. A shell or Python row prints
// the same line (tests/gi/rough_lag.sh).
#include <cstdio>
#include <string>

namespace enginetest {
inline void lawLine(const char *name, double value, const char *unit, const char *op = nullptr, double bar = 0.0)
{
    if (op) std::printf("LAW %s %.9g %s %s %.9g\n", name, value, unit, op, bar);
    else std::printf("LAW %s %.9g %s\n", name, value, unit);
    std::fflush(stdout);
}
/// A name part from free text (a region's, a tier's): every character outside [A-Za-z0-9_.:+-] becomes '_'.
inline std::string lawName(std::string s)
{
    for (char &c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == ':' || c == '+' || c == '-'))
            c = '_';
    return s;
}
} // namespace enginetest
