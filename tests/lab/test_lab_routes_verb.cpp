// lab.routes_verb — THE LAB's ROUTE DECLARATION, its grammar and its judgement (PHOTON-7-9A; src/lab/labcommands.h).
// CPU: the parser and the judge are pure (labcommands.cpp alone). LAW (enginetest::requireRoutes' own, for jahlab rows):
// the chain's routes (gather, integrate, reflection) and the pause match EXACTLY - a route the frame ran and the line
// did not name is a mismatch too; a declared product (field, cards, store) must be live by the 16th frame after the
// declaration (before it: Wait); every word once, from the set; `none` alone.
#include <cstdio>
#include <string>
#include "labcommands.h"

using namespace lab;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    Command c;
    std::string err;
    check(parse("routes gather reflection store", c, err) && c.verb == "routes" && c.args.size() == 3, "the line parses");
    RouteSet d, a;
    check(parseRoutes({ "gather", "reflection", "store" }, d, err) && d.gather && d.reflection && d.store && !d.integrate,
          "the words set their routes");
    check(routesText(d) == "gather reflection store", "the text is the helper's order");
    check(!parseRoutes({ "gather", "gather" }, a, err) && !parseRoutes({ "rays" }, a, err) && !parseRoutes({}, a, err),
          "a word twice, an unknown word and no word are refused");
    check(parseRoutes({ "none" }, a, err) && routesText(a) == "none", "`none` alone is the empty set");
    // the judgement
    a = d;
    check(judgeRoutes(d, a, 1) == RouteVerdict::Match, "the same set: a match on frame 1");
    a = d; a.integrate = true;
    check(judgeRoutes(d, a, 1) == RouteVerdict::Mismatch, "a route the frame ran and the line did not name: a mismatch");
    a = d; a.reflection = false;
    check(judgeRoutes(d, a, 1) == RouteVerdict::Mismatch, "a declared chain route missing: a mismatch at once");
    a = d; a.paused = true;
    check(judgeRoutes(d, a, 1) == RouteVerdict::Mismatch, "the pause is exact");
    a = d; a.store = false;
    check(judgeRoutes(d, a, 1) == RouteVerdict::Wait && judgeRoutes(d, a, kRouteProductFrames - 1) == RouteVerdict::Wait,
          "a declared product not live yet: wait until frame 15");
    check(judgeRoutes(d, a, kRouteProductFrames) == RouteVerdict::Mismatch, "...and a mismatch at frame 16");
    a = d; a.cards = true;
    check(judgeRoutes(d, a, 1) == RouteVerdict::Match, "a live product the line did not declare is no mismatch (products: declared => live)");
    std::printf("%s\n", gFail ? "lab.routes_verb: FAILED" : "lab.routes_verb: PASSED");
    return gFail ? 1 : 0;
}
