// jahlab — the engine-only harness (LAB-1; SPECS/LAB_1_SPEC.md, DOCS/LAB.md): one window (or an offscreen view), one
// model through the real import + bake, the engine's full frame — Atom's cut, Photon at the chosen tier, the sun and
// sky — and ONE command surface (labcommands.h) that the keys, --script and stdin share. Links the engine, irisgl's
// document / import / mirror and Qt Core/Gui; nothing of Studio's src/ (no services, no UI, no scripting host, no
// QCoreApplication, no widgets).
#include "labcommands.h"
#include "labsession.h"

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
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--help" || a == "-h") { std::printf("%s", lab::helpText().c_str()); return 0; }
        else if (a == "--headless") opt.headless = true;
        else if (a == "--no-stdin") opt.readStdin = false;
        else if (a == "--bake-only") opt.bakeOnly = true;
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
    if (opt.file.isEmpty()) return usage("no scene file");
    if (opt.tier.isEmpty() && !opt.file.endsWith(QStringLiteral(".jlab"), Qt::CaseInsensitive)) opt.tier = QStringLiteral("epic");
    lab::Session session;
    return session.run(opt);
}
