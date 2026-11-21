// engine.stillness_history_column — THE STILLNESS RECORD CARRIES THE HISTORY's FLOOR h ITSELF (ROUGH-LOBE-HISTORY-1;
// irisgl Stillness.cpp's StillnessReport::tsv, GatherStatus::historyFrames). No device: a CPU code row on the record's text.
// WHY: gi.walk_rough.k<k>'s lag law needs h (the EMA's 1/h once full); the record held only the settle N = settleFramesOf(h),
// and the row inverted it - a read that depended on a constant of the settle's formula. The record now carries h.
// LAW: the header names `history_frames`, every row has the header's column count, and a frame's historyFrames is the value
// written under that column in BOTH parts of a window (its trail, "<name>-move", and its frames) - beside settle_frames,
// which stays the rest mean's length.
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "jahshaka/engine/Stillness.h"

using namespace jahshaka::engine;
static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

static std::vector<std::string> fields(const std::string &line)
{
    std::vector<std::string> out;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, '\t')) out.push_back(cell);
    if (!line.empty() && line.back() == '\t') out.push_back("");
    return out;
}

int main()
{
    StillnessReport r;
    StillnessWindow w;
    w.name = "walk";
    StillnessFrame f;
    f.classes.resize(1);
    f.settleFrames = 16u;
    f.historyFrames = 10u;
    f.index = 3u;
    w.trail.push_back(f);
    f.historyFrames = 7u;   // (the A/B door's floor: a frame after a tuning push)
    f.settleFrames = 11u;
    w.frames.push_back(f);
    r.windows.push_back(w);
    std::istringstream text(r.tsv());
    std::string line;
    std::vector<std::vector<std::string>> rows;
    while (std::getline(text, line)) rows.push_back(fields(line));
    check(rows.size() == 3u, "the record holds the header and the window's two frames");
    if (rows.size() != 3u) return 1;
    const std::vector<std::string> &head = rows[0];
    int hcol = -1, scol = -1;
    for (size_t i = 0; i < head.size(); ++i) {
        if (head[i] == "history_frames") hcol = int(i);
        if (head[i] == "settle_frames") scol = int(i);
    }
    check(hcol >= 0, "the header names history_frames");
    check(scol >= 0, "the header keeps settle_frames (the rest mean's length)");
    check(rows[1].size() == head.size() && rows[2].size() == head.size(), "every row has the header's column count");
    if (hcol < 0 || scol < 0 || rows[1].size() != head.size() || rows[2].size() != head.size()) return 1;
    check(rows[1][0] == "walk-move" && rows[1][size_t(hcol)] == "10" && rows[1][size_t(scol)] == "16",
          "the trail's frame: history_frames 10 beside settle_frames 16");
    check(rows[2][0] == "walk" && rows[2][size_t(hcol)] == "7" && rows[2][size_t(scol)] == "11",
          "the window's frame: history_frames 7 beside settle_frames 11 (h is written, never derived from the settle)");
    std::printf("%s\n", gFail ? "engine.stillness_history_column: FAILED" : "engine.stillness_history_column: PASSED");
    return gFail ? 1 : 0;
}
