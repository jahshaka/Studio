// jahlab — the engine-only harness (LAB-1; SPECS/LAB_1_SPEC.md, DOCS/LAB.md): one window (or an offscreen view), one
// model through the real import + bake, the engine's full frame — Atom's cut, Photon at the chosen tier, the sun and
// sky — and ONE command surface (labcommands.h) that the keys, --script and stdin share. Links the engine, irisgl's
// document / import / mirror and Qt Core/Gui; nothing of Studio's src/ (no services, no UI, no scripting host, no
// QCoreApplication, no widgets).
#include "jahshaka/engine/ProcessExit.h"
#include "labcommands.h"
#include "labsession.h"
#include "envs/env.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

QString binaryDir()
{
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return QDir::currentPath();
    buf[n] = 0;
    return QFileInfo(QString::fromLocal8Bit(buf)).absolutePath();
}

/// --env-list: the catalogue, each environment's law and its prediction at the dolly's stops (CPU only: the geometry and
/// its feature table, no bake, no engine) under the default optics (1920 x 1080, 45 degrees, Epic's 1 px).
int envList(unsigned seed, int footprint)
{
    using namespace lab::envs;
    for (const Env &e : catalogue()) {
        const Built b = e.build(seed, false);
        std::printf("%s (seed %u): %u meshes, %u instances, %llu triangles, %zu features\n  %s\n", e.name, seed, b.meshes,
                    b.instances, b.triangles, b.features.size(), e.describe().c_str());
        for (double d : stops()) {
            Optics o;
            e.stop(d, o.eye, o.target);
            o.farClip = e.farClip;
            o.footprint = footprint;
            for (const Prediction &p : e.expect(b, o))
                std::printf("  EXPECT %s d=%.0f m: %s = %.10g  (%s)\n", e.name, d, p.quantity.c_str(), p.value, p.note.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

/// --env-check [--env NAME]: every environment (or the one named) built whole and baked (timed: under 5 s), rebuilt
/// and baked AGAIN with the same seed — the same triangles, the same scene hash AND the same bake hash (the bake pool's
/// threaded output inside the determinism check) — and built with the next seed (another scene hash: the seed reaches
/// the geometry).
int envCheck(unsigned seed, const std::string &only)
{
    using namespace lab::envs;
    int bad = 0, n = 0;
    for (const Env &e : catalogue()) {
        if (!only.empty() && only != e.name) continue;
        ++n;
        const Built a = e.build(seed, true);
        const Built again = e.build(seed, true);
        const Built other = e.build(seed + 1, false);
        const bool fast = a.buildMs <= 5000.0, same = a.hash == again.hash && a.triangles == again.triangles,
                   baked = a.bakeHash == again.bakeHash, seeded = a.hash != other.hash;
        std::printf("ENV-CHECK %-15s build %6.0f ms (bake %6.0f ms)%s | %8llu triangles, hash %s | rebuilt %s | bake %s%s%s "
                    "| seed+1 %s\n", e.name, a.buildMs, a.bakeMs, fast ? "" : " OVER 5 s", a.triangles,
                    a.hash.left(16).constData(), same ? "identical" : "DIFFERS", a.bakeHash.left(16).constData(),
                    baked ? " = rebaked" : " != REBAKED ", baked ? "" : again.bakeHash.left(16).constData(),
                    seeded ? "another hash" : "THE SAME HASH (the seed reaches nothing)");
        bad += !(same && baked && seeded);   // the time is printed, never a bar (the merge read's F3: no wall-clock verdict in a row)
    }
    if (n == 0) { std::printf("ENV-CHECK: no environment '%s'\n", only.c_str()); return 1; }
    std::printf("ENV-CHECK %s (%d of %d red)\n", bad ? "FAILED" : "PASSED", bad, n);
    return bad ? 1 : 0;
}

bool sixFloats(const char *s, iris::Vec3 &a, iris::Vec3 &b)
{
    float v[6];
    if (std::sscanf(s, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    a = iris::Vec3(v[0], v[1], v[2]);
    b = iris::Vec3(v[3], v[4], v[5]);
    return true;
}

}   // namespace

int main(int argc, char **argv)
{
    lab::Options opt;
    opt.binDir = binaryDir();
    const char *home = std::getenv("HOME");
    opt.dataRoot = QDir(QString::fromLocal8Bit(home ? home : "/tmp")).filePath(QStringLiteral(".local/share/jahlab"));
    opt.tier.clear();   // empty = the .jlab's, else epic
    const auto usage = [](const char *why) {
        std::fprintf(stderr, "jahlab: %s (jahlab --help)\n", why);
        return 2;
    };
    bool envListWanted = false, envCheckWanted = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--help" || a == "-h") { std::printf("%s", lab::helpText().c_str()); return 0; }
        else if (a == "--headless") opt.headless = true;
        else if (a == "--no-stdin") opt.readStdin = false;
        else if (a == "--bake-only") opt.bakeOnly = true;
        else if (a == "--env") { const char *v = next(); if (!v) return usage("--env needs a name (--env-list)"); opt.env = QString::fromLocal8Bit(v); }
        else if (a == "--seed") { const char *v = next(); char *end = nullptr; if (!v) return usage("--seed needs a number"); opt.seed = unsigned(std::strtoul(v, &end, 10)); if (!end || *end) return usage("--seed needs a number"); }
        else if (a == "--env-list") envListWanted = true;
        else if (a == "--footprint") { const char *v = next(); if (!v || std::atoi(v) < 1) return usage("--footprint needs texels >= 1"); opt.footprint = std::atoi(v); }
        else if (a == "--env-check") envCheckWanted = true;
        else if (a == "--data-root") { const char *v = next(); if (!v) return usage("--data-root needs a directory"); opt.dataRoot = QDir(QString::fromLocal8Bit(v)).absolutePath(); }
        else if (a == "--script") { const char *v = next(); if (!v) return usage("--script needs a file"); opt.script = QString::fromLocal8Bit(v); }
        else if (a == "--tier") { const char *v = next(); if (!v) return usage("--tier needs low|medium|high|epic"); opt.tier = QString::fromLocal8Bit(v); }
        else if (a == "--camera") { const char *v = next(); if (!v || !sixFloats(v, opt.camPos, opt.camTarget)) return usage("--camera x,y,z,tx,ty,tz"); opt.hasCamera = true; }
        else if (a == "--size") {
            const char *v = next();
            unsigned w = 0, h = 0;
            if (!v || std::sscanf(v, "%ux%u", &w, &h) != 2 || w < 16 || h < 16) return usage("--size WxH");
            opt.width = w; opt.height = h;
        }
        else if (!a.empty() && a[0] == '-') return usage(("unknown flag " + a).c_str());
        else if (opt.file.isEmpty()) opt.file = QString::fromLocal8Bit(argv[i]);
        else return usage("one scene file only");
    }
    if (envListWanted) return envList(opt.seed, opt.footprint);
    if (envCheckWanted) return envCheck(opt.seed, opt.env.toStdString());
    if (!opt.env.isEmpty() && !opt.file.isEmpty()) return usage("a file or --env, not both");
    if (!opt.env.isEmpty() && !lab::envs::find(opt.env.toStdString())) return usage("no such environment (--env-list)");
    if (opt.file.isEmpty() && opt.env.isEmpty()) return usage("no scene file (or --env <name>)");
    if (opt.tier.isEmpty() && (!opt.env.isEmpty() || !opt.file.endsWith(QStringLiteral(".jlab"), Qt::CaseInsensitive)))
        opt.tier = QStringLiteral("epic");
    // MEASURING ARM (URGENT #102's ABBA, to be reverted): JAH_U102_FASTEXIT=1 ends the run by the exit-only teardown.
    const bool fastExit = qEnvironmentVariableIntValue("JAH_U102_FASTEXIT") == 1;
    if (fastExit) jahshaka::engine::markProcessExit();
    int rc = 0;
    {
        lab::Session session;
        rc = session.run(opt);
    }
    if (fastExit) { std::fflush(nullptr); std::_Exit(rc); }
    return rc;
}
