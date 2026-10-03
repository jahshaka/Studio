// document.cascade_set_reader — THE PINNED CASCADE TABLE IS READ CLAMPED (PHOTON-II-1, the
// merge read's worth-a-look 4): a scene file's `giCascadeSet` past the renderer's slot table
// (kGiTierMaxCascades) keeps its first rows, drops the rest and SAYS how many it dropped —
// the open raises `gi.cascades.clamped` with that count (SceneIssues::raiseCascadeSetClamped);
// the mirror never sees, and never silently truncates, a longer table. Malformed rows are not
// rows. No GPU, no display.
#include "io/cascadesetformat.h"

#include <QJsonArray>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        std::printf("%s: %s\n", (cond) ? "ok" : "FAIL", msg);                   \
        if (!(cond)) ++failures;                                                \
    } while (0)

static QJsonArray rowsOf(int n)
{
    QJsonArray a;
    for (int i = 0; i < n; ++i) a.append(QJsonArray{ 5.0 * (i + 1), 64.0, 0.0 });
    return a;
}

int main()
{
    const int cap = jahshaka::engine::kGiTierMaxCascades;
    QVector<iris::Vec3> out;
    CHECK(sceneformat::readCascadeSet(rowsOf(cap), out) == 0 && out.size() == cap,
          "a table at the ceiling reads whole, nothing dropped");
    CHECK(sceneformat::readCascadeSet(rowsOf(cap + 3), out) == 3 && out.size() == cap,
          "a longer table keeps its first rows and reports the three past the ceiling");
    CHECK(out.size() == cap && out[cap - 1].x() == float(5.0 * cap) && out[0].x() == 5.0f,
          "...the KEPT rows are the innermost ones, in order");
    QJsonArray mixed = rowsOf(2);
    mixed.append(QJsonArray{ 1.0, 2.0 });   // malformed: not a row
    CHECK(sceneformat::readCascadeSet(mixed, out) == 0 && out.size() == 2,
          "a malformed row is not a row (and is not counted as dropped)");
    CHECK(sceneformat::readCascadeSet(QJsonArray(), out) == 0 && out.isEmpty(),
          "an absent table reads empty: the tier decides");
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
