// engine.ray_input_guard — A RAY JOB WITH A NULL INPUT VIEW REFUSES ITS DISPATCH AND SAYS SO ONCE (GATHER-HANG-1 stage 2;
// irisgl/engine/src/RayInputGuard.h says the law, RayQueryViewCache.h the views). No device, no Ogre.
// LAW: every view made = admitted, nothing counted or said; any view null = refused, counted every time, said the FIRST
// time for that job only (with the job's name and how many were null); jobs are said apart. And THE FORCED NON-RESIDENT
// INPUT: RayQueryTier::textureView hands the cache image 0 for a texture that is not resident; the cache answers a null
// view and caches and registers nothing, and the job that binds it refuses - before the guard it was viewed on the
// manager's blank image and traced as the input.
// AND THE ORDER IN THE SOURCE (argv[1] = irisgl/engine/src/OgreRayQuery.cpp; stage 2 item 6b): every ray job of the
// tier calls the guard BEFORE the state its frame advances, looks up no view of a texture after that state moved, and
// a job that writes the hit set checks the hit set's inputs (hitSetInputsMade) before it too - a job that refused after
// advancing would leave its next frame reading a state that never happened. Each job's ADVANCE is named below, a token
// of the state its frame moves (a ring, a history, a registration, a frame's layouts); a token not found is a FAIL (the
// table is stale, never skipped). Red first on the base, where no job of the tier called the guard
// (spikes/gather-hang-1/stage2/guard-jobs-red-first.txt).
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

// The body of `RayQueryTier::<name>(...) {...}` (its definition: the parameter list followed by '{'), or empty.
static std::string bodyOf(const std::string &code, const std::string &name)
{
    const std::string head = "RayQueryTier::" + name + "(";
    for (size_t at = code.find(head); at != std::string::npos; at = code.find(head, at + 1)) {
        size_t i = at + head.size();
        for (int depth = 1; i < code.size() && depth > 0; ++i) depth += code[i] == '(' ? 1 : code[i] == ')' ? -1 : 0;
        while (i < code.size() && (code[i] == ' ' || code[i] == '\n' || code[i] == '\t')) ++i;
        if (i >= code.size() || code[i] != '{') continue;   // a call or a declaration
        const size_t open = i;
        for (int depth = 0; i < code.size(); ++i) {
            depth += code[i] == '{' ? 1 : code[i] == '}' ? -1 : 0;
            if (depth == 0) return code.substr(open, i + 1 - open);
        }
    }
    return std::string();
}

static void sourceOrder(const char *path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string code = codeOnly(ss.str());
    check(!code.empty(), std::string("5. the source reads: ") + path);
    struct Job { const char *name; const char *kind; const char *advances[2]; };
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
    };
    for (const Job &j : jobs) {
        const std::string body = bodyOf(code, j.name);
        const std::string who = std::string("5. ") + j.name + " (" + j.kind + ")";
        if (body.empty()) { check(false, who + ": its definition is found"); continue; }
        size_t advance = std::string::npos;
        bool tokens = true;
        for (const char *t : j.advances) {
            if (!t) continue;
            const size_t at = body.find(t);
            tokens = tokens && at != std::string::npos;
            advance = std::min(advance, at);
        }
        if (!tokens) { check(false, who + ": every advance token is found (the table is stale)"); continue; }
        const size_t guard = body.find("inputsMade(");
        check(guard < advance, who + ": calls the guard before the state its frame advances");
        check(body.find("textureView(", advance) == std::string::npos,
              who + ": looks up no texture view after that state moved");
        if (body.find("hitSet.write(") != std::string::npos)
            check(body.find("hitSetInputsMade(") < advance, who + ": checks its hit set's inputs before that state");
    }
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
    check(argc > 1, "5. the tier's source is given (argv[1])");
    if (argc > 1) sourceOrder(argv[1]);
    if (gFail) std::printf("engine.ray_input_guard: FAILED (%d)\n", gFail);
    else std::printf("engine.ray_input_guard: PASSED\n");
    return gFail ? 1 : 0;
}
