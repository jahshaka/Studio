// lab.strip_hits — THE BUTTON STRIP's GEOMETRY (LAB-SCRIPTS-1 stage 2; src/lab/labcommands.h stripButtons / stripHit).
// CPU: the strip's table is the lab's command surface (a click is a command LINE through the one parser, as a key is).
// LAW: the three buttons are Run `js run`, Load `js next`, Clear `js clear`, left to right; a click at a button's first
// column (x0) and its last (x1 - 1), on the strip's top row (0) and its bottom row (kStripHeight - 1), runs that button's
// line; a click one past its last column (x1: the gap), above the strip (-1), below it (kStripHeight) or past the last
// button runs nothing; every button's line parses as the js verb.
// CONTROL: the hits counted are above zero (a table that maps nothing would pass the misses alone).
#include <cstdio>
#include <string>
#include "labcommands.h"

static int gFail = 0;
static void check(bool ok, const std::string &what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str()); if (!ok) ++gFail; }

int main()
{
    const auto &b = lab::stripButtons();
    check(b.size() == 3 && std::string(b[0].label) == "Run" && std::string(b[0].line) == "js run" &&
              std::string(b[1].label) == "Load" && std::string(b[1].line) == "js next" &&
              std::string(b[2].label) == "Clear" && std::string(b[2].line) == "js clear",
          "the strip is Run `js run`, Load `js next`, Clear `js clear`, left to right");
    int hits = 0, edges = 0, misses = 0;
    for (const lab::StripButton &s : b) {
        const std::string line = s.line;
        for (int x : { s.x0, s.x1 - 1 })
            for (int y : { 0, lab::kStripHeight - 1 }) {
                ++edges;
                if (lab::stripHit(x, y) == line) ++hits;
            }
        for (const auto &p : { std::make_pair(s.x1, 0), std::make_pair(s.x0, -1), std::make_pair(s.x0, lab::kStripHeight) })
            if (lab::stripHit(p.first, p.second).empty()) ++misses;
        lab::Command c;
        std::string err;
        check(lab::parse(line, c, err) && c.verb == "js", "`" + line + "` (" + s.label + ") parses as the js verb");
    }
    check(hits == edges, "every button's four corners run its own line (" + std::to_string(hits) + " of " +
                             std::to_string(edges) + ")");
    check(misses == 3 * int(b.size()), "one past each button, above and below the strip run nothing (" +
                                           std::to_string(misses) + " of " + std::to_string(3 * b.size()) + ")");
    check(b.empty() || (lab::stripHit(b.back().x1 + 100, 1).empty() && lab::stripHit(lab::kStripTextX, 1).empty()),
          "the strip's text, past the last button, runs nothing");
    check(hits > 0, "control: the hits counted are above zero (" + std::to_string(hits) + ")");
    std::printf("%s\n", gFail ? ("lab.strip_hits: FAILED (" + std::to_string(gFail) + ")").c_str() : "lab.strip_hits: PASSED");
    return gFail ? 1 : 0;
}
