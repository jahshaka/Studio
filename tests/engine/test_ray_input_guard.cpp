// engine.ray_input_guard — A RAY JOB WITH A NULL INPUT VIEW REFUSES ITS DISPATCH AND SAYS SO ONCE (GATHER-HANG-1 stage 2;
// irisgl/engine/src/RayInputGuard.h says the law, RayQueryViewCache.h the views). No device, no Ogre.
// LAW: every view made = admitted, nothing counted or said; any view null = refused, counted every time, said the FIRST
// time for that job only (with the job's name and how many were null); jobs are said apart. And THE FORCED NON-RESIDENT
// INPUT: RayQueryTier::textureView hands the cache image 0 for a texture that is not resident; the cache answers a null
// view and caches and registers nothing, and the job that binds it refuses - before the guard it was viewed on the
// manager's blank image and traced as the input.
// AND THE ORDER IN THE SOURCE (argv[1] = irisgl/engine/src/OgreRayQuery.cpp, argv[2] = OgreScreenProbeGather.cpp; stage 2
// item 6b; the job list DERIVED from the source since the final read's item 5): every ray job of the
// tier calls the guard BEFORE the state its frame advances, looks up no view of a texture after that state moved, and
// a job that writes the hit set checks the hit set's inputs (hitSetInputsMade) before it too - a job that refused after
// advancing would leave its next frame reading a state that never happened. Each job's ADVANCE is named below, a token
// of the state its frame moves (a ring, a history, a registration, a frame's layouts); a token not found is a FAIL (the
// table is stale, never skipped). Red first on the base, where no job of the tier called the guard
// (spikes/gather-hang-1/stage2/guard-jobs-red-first.txt).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef JAH_RAY_INPUT_GUARD_UNDER_TEST   // (spikes/gather-hang-1/stage2 runs these cases on the base's silent decline as the red)
#include "RayInputGuard.h"
#define JAH_RAY_INPUT_GUARD_UNDER_TEST jahshaka::engine::detail::RayInputGuard
#endif
#include "RayQueryViewCache.h"

using Guard = JAH_RAY_INPUT_GUARD_UNDER_TEST;
using jahshaka::engine::detail::RayQueryViewCache;
using jahshaka::engine::detail::RayViewKey;
using jahshaka::engine::detail::RayViewKind;

static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }
static void check(bool ok, const std::string &what) { check(ok, what.c_str()); }

// The source with its comments and the contents of its string and character literals blanked (a comment or a log line
// that names the guard is not a call of it).
static std::string codeOnly(const std::string &s)
{
    std::string out(s);
    enum { Code, Line, Block, Str, Chr } st = Code;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i], n = i + 1 < s.size() ? s[i + 1] : '\0';
        switch (st) {
        case Code:
            if (c == '/' && n == '/') { st = Line; out[i] = ' '; }
            else if (c == '/' && n == '*') { st = Block; out[i] = ' '; }
            else if (c == '"') st = Str;
            else if (c == '\'') st = Chr;
            break;
        case Line: if (c == '\n') st = Code; else out[i] = ' '; break;
        case Block:
            out[i] = c == '\n' ? '\n' : ' ';
            if (c == '*' && n == '/') { out[++i] = ' '; st = Code; }
            break;
        case Str: case Chr:
            if (c == '\\') { out[i] = ' '; if (i + 1 < s.size()) out[++i] = ' '; }
            else if (c == (st == Str ? '"' : '\'')) st = Code;
            else out[i] = ' ';
            break;
        }
    }
    return out;
}

// Every DEFINITION `<Class>::<name>(...) {...}` of the two classes in `code`: its name and its body.
struct Def { std::string name, body; };
static std::vector<Def> definitions(const std::string &code, const std::string &cls)
{
    std::vector<Def> out;
    const std::string head = cls + "::";
    for (size_t at = code.find(head); at != std::string::npos; at = code.find(head, at + 1)) {
        size_t i = at + head.size(), n = i;
        while (n < code.size() && (std::isalnum(static_cast<unsigned char>(code[n])) || code[n] == '_')) ++n;
        if (n == i || n >= code.size() || code[n] != '(') continue;
        const std::string name = code.substr(i, n - i);
        i = n + 1;
        for (int depth = 1; i < code.size() && depth > 0; ++i) depth += code[i] == '(' ? 1 : code[i] == ')' ? -1 : 0;
        while (i < code.size() && (std::isspace(static_cast<unsigned char>(code[i])) || code.compare(i, 5, "const") == 0))
            i += std::isspace(static_cast<unsigned char>(code[i])) ? 1 : 5;
        if (i >= code.size() || code[i] != '{') continue;   // a call or a declaration
        const size_t open = i;
        for (int depth = 0; i < code.size(); ++i) {
            depth += code[i] == '{' ? 1 : code[i] == '}' ? -1 : 0;
            if (depth == 0) break;
        }
        out.push_back({ name, code.substr(open, i + 1 - open) });
    }
    return out;
}

static bool hasCall(const std::string &body, const std::string &fn, size_t from = 0)
{
    for (size_t at = body.find(fn, from); at != std::string::npos; at = body.find(fn, at + 1)) {
        const bool wordStart = at == 0 || !(std::isalnum(static_cast<unsigned char>(body[at - 1])) || body[at - 1] == '_');
        size_t k = at + fn.size();
        while (k < body.size() && body[k] == ' ') ++k;
        if (wordStart && k < body.size() && body[k] == '(') return true;
    }
    return false;
}
static size_t firstCall(const std::string &body, const std::string &fn)
{
    for (size_t at = body.find(fn); at != std::string::npos; at = body.find(fn, at + 1)) {
        const bool wordStart = at == 0 || !(std::isalnum(static_cast<unsigned char>(body[at - 1])) || body[at - 1] == '_');
        size_t k = at + fn.size();
        while (k < body.size() && body[k] == ' ') ++k;
        if (wordStart && k < body.size() && body[k] == '(') return at;
    }
    return std::string::npos;
}

// A RAY JOB, DERIVED FROM THE SOURCE (the final read's item 5): a definition of RayQueryTier or ScreenProbeGather that looks
// up a texture view (textureView, gatherView, a hit set's write) AND records a command (vkCmd...). Each must be in the
// table with its advance: a job the table does not name (a 12th) is a FAIL until it is guarded and named.
static void sourceOrder(const char *tierPath, const char *gatherPath)
{
    std::string code;
    for (const char *path : { tierPath, gatherPath }) {
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        check(!ss.str().empty(), std::string("5. the source reads: ") + path);
        code += codeOnly(ss.str()) + "\n";
    }
    // `views` (optional): the token before which EVERY view the job binds - its own targets too - is looked up AND
    // passed through the guard (the final re-read's C1: the gather's own three targets were viewed after its ring)
    struct Job { const char *name; const char *kind; const char *advances[2]; const char *views = nullptr; };
    // (each advance: the first token of the state the job's frame moves - named at its line in the lane's report)
    const Job jobs[] = {
        { "recordReflect", "a history job", { "rv.ringFrame++", nullptr } },
        { "recordMotion", "a reading job", { "mReflects[key]", nullptr } },
        { "recordAreaVis", "a history job", { "areaVisFrame(", nullptr } },
        { "recordSunContact", "a reading job", { "sv.params[ring]", nullptr } },
        { "recordHitComposite", "a history job", { "hv.params[ring].mapped", nullptr } },
        { "traceGlassEscape", "a reading job", { "ev.frame % kReflectRing", nullptr } },
        { "prepareHitList", "a reading job", { "createUavBuffer(", "vkCmdFillBuffer(" } },
        { "traceCardMovers", "a card job", { "mCardLamps[scene]", nullptr } },
        { "gatherCards", "a card job", { "mCardGather[scene]", nullptr } },
        { "gatherVoxelSides", "a history job", { "foldVoxelSideCounts(", "++v.sharesWrites" } },
        { "cardPickBlocking", "a card job", { "executeResourceTransition(", "flushCommands(" } },
        { "record", "the probe gather (a history job)", { "rebindScene(", nullptr }, "v.frame % kRing" },
    };
    std::vector<Def> defs = definitions(code, "RayQueryTier");
    for (Def &d : definitions(code, "ScreenProbeGather")) defs.push_back(d);
    int derived = 0;
    for (const Def &d : defs) {
        const bool views = hasCall(d.body, "textureView") || hasCall(d.body, "gatherView") ||
                           d.body.find("hitSet.write(") != std::string::npos;
        const bool records = d.body.find("vkCmd") != std::string::npos;
        if (!views || !records) continue;
        ++derived;
        const Job *j = nullptr;
        for (const Job &c : jobs) if (d.name == c.name) j = &c;
        if (!j) { check(false, "5. " + d.name + ": a ray job (it looks up a view and records a command) the table does not name: guard it and name its advance"); continue; }
        const std::string who = std::string("5. ") + j->name + " (" + j->kind + ")";
        size_t advance = std::string::npos;
        bool tokens = true;
        for (const char *t : j->advances) {
            if (!t) continue;
            const size_t at = d.body.find(t);
            tokens = tokens && at != std::string::npos;
            advance = std::min(advance, at);
        }
        if (!tokens) { check(false, who + ": every advance token is found (the table is stale)"); continue; }
        const size_t guard = std::min({ firstCall(d.body, "inputsMade"), firstCall(d.body, "gatherInputsMade"),
                                        firstCall(d.body, "hitSetInputsMade") });
        check(guard < advance, who + ": calls the guard before the state its frame advances");
        const std::string after = d.body.substr(advance);
        // (a lookup of an INPUT after the advance: the tier's textureView or its guard's input lambda; the gather's
        // gatherView of `in.` - its own images, made or the gather fails, may be viewed where they are bound)
        const bool late = hasCall(after, "textureView") || hasCall(after, "input") ||
                          after.find("gatherView(in.") != std::string::npos ||
                          after.find("gatherView( in.") != std::string::npos;
        check(!late, who + ": looks up no input view after that state moved");
        if (d.body.find("hitSet.write(") != std::string::npos)
            check(firstCall(d.body, "hitSetInputsMade") < advance, who + ": checks its hit set's inputs before that state");
        if (j->views) {
            const size_t ring = d.body.find(j->views);
            if (ring == std::string::npos) { check(false, who + ": its views token is found (the table is stale)"); continue; }
            const std::string tail = d.body.substr(ring);
            check(!hasCall(tail, "gatherView") && !hasCall(tail, "textureView"),
                  who + ": looks up EVERY view it binds - its own targets too - before " + j->views);
            const std::string head = d.body.substr(0, ring);
            size_t last = 0u;
            for (const char *fn : { "gatherView", "textureView" })
                for (size_t from = 0u, at; (at = firstCall(head.substr(from), fn)) != std::string::npos; from += at + 1u)
                    last = std::max(last, from + at);
            const std::string between = d.body.substr(last, ring - last);
            check(hasCall(between, "gatherInputsMade") || hasCall(between, "inputsMade"),
                  who + ": the guard checks its last views before " + j->views);
        }
    }
    for (const Job &c : jobs) {
        bool found = false;
        for (const Def &d : defs) found = found || d.name == c.name;
        if (!found) check(false, std::string("5. ") + c.name + ": in the table but not in the source (the table is stale)");
    }
    char line[96];
    std::snprintf(line, sizeof(line), "5. the jobs derived from the source: %d (the table names %zu)", derived,
                  sizeof(jobs) / sizeof(jobs[0]));
    check(derived == int(sizeof(jobs) / sizeof(jobs[0])), line);
}

int main(int argc, char **argv)
{
    {   // 1. every view made: admitted, nothing counted, nothing said
        Guard g;
        std::string say = "x";
        check(g.admit("reflect", { 0x11u, 0x12u, 0x13u }, &say) && g.refused() == 0u && say.empty(),
              "1. every view made: admitted, nothing counted, nothing said");
        check(g.admit("reflect", {}, &say) && g.refused() == 0u, "1. a job with no views is admitted");
    }
    {   // 2. a null view: refused and said the first time; the second refusal counted, not said
        Guard g;
        std::string first, second;
        const bool a = g.admit("probe gather", { 0x11u, 0u, 0x13u, 0u }, &first);
        const bool b = g.admit("probe gather", { 0u }, &second);
        check(!a && !b && g.refused() == 2u && g.refused("probe gather") == 2u,
              "2. a null view refuses the dispatch, every time, each refusal counted");
        check(first.find("probe gather") != std::string::npos && first.find("2 of 4") != std::string::npos &&
                  first.find("first at 1") != std::string::npos,
              "2. the first refusal is said, naming the job and the null views (2 of 4, the first at 1)");
        check(second.empty(), "2. the second refusal of the same job is not said again");
    }
    {   // 3. jobs are said apart; an admitted job in between changes nothing
        Guard g;
        std::string a, b, c;
        g.admit("reflect", { 0u }, &a);
        g.admit("area vis", { 0x21u }, &b);
        g.admit("area vis", { 0u }, &c);
        check(!a.empty() && b.empty() && !c.empty() && g.refused() == 2u && g.refused("reflect") == 1u &&
                  g.refused("area vis") == 1u,
              "3. each job's first refusal is said once, apart from the others'");
    }
    {   // 4. THE FORCED NON-RESIDENT INPUT: image 0 from the tier's rule -> a null view -> the job refuses, said once
        int makes = 0, watches = 0;
        RayQueryViewCache cache{ [&](const RayViewKey &, uint64_t image) -> uint64_t { ++makes; return image + 0x1000u; },
                                 [](uint64_t) {}, [&](const void *, bool on) { if (on) ++watches; } };
        int volume = 0, sky = 0;
        const auto viewOf = [&](const void *t, bool resident, uint64_t image) {
            RayViewKey k;
            k.texture = t;
            k.kind = RayViewKind::Sampled;
            return cache.view(k, resident ? image : 0u);   // RayQueryTier::textureView's rule: not resident -> image 0
        };
        Guard g;
        std::string say;
        const uint64_t vSky = viewOf(&sky, true, 0xB1u);
        const uint64_t vVolume = viewOf(&volume, false, 0xA1u);   // the voxel volume forced OnStorage
        const bool admitted = g.admit("probe gather", { vSky, vVolume }, &say);
        check(vSky != 0u && vVolume == 0u && makes == 1 && watches == 1 && cache.live() == 1u,
              "4. a non-resident texture gets no view: nothing made, cached or registered for it");
        check(!admitted && !say.empty() && g.refused("probe gather") == 1u,
              "4. the job binding it refuses its dispatch and says so");
        const uint64_t again = viewOf(&volume, true, 0xA1u);   // resident again
        check(again != 0u && g.admit("probe gather", { vSky, again }, &say) && say.empty(),
              "4. resident again: the view is made and the job is admitted");
    }
    check(argc > 2, "5. the tier's and the gather's sources are given (argv[1], argv[2])");
    if (argc > 2) sourceOrder(argv[1], argv[2]);
    if (gFail) std::printf("engine.ray_input_guard: FAILED (%d)\n", gFail);
    else std::printf("engine.ray_input_guard: PASSED\n");
    return gFail ? 1 : 0;
}
