// display_guard_probe.cpp — source.display_guard_toy's probe (tests/support/display_guard.cpp). Built twice: guarded like
// every test binary (display_guard_probe) and as the toy's NEGATIVE CONTROL, unguarded (display_guard_probe_unguarded).
//   --env-only   prints what the guard left (DISPLAY, WAYLAND_DISPLAY, QT_QPA_PLATFORM) and exits 0 BEFORE any
//                Q*Application exists: a case run with a literal DISPLAY=:0 can reach no display even if the guard is broken.
//   (no flag)    constructs a QGuiApplication and prints platform=<its platform name>.
#include <QGuiApplication>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--env-only") == 0) {
        const char *names[] = {"DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "QT_QPA_PLATFORM"};
        for (const char *n : names) {
            const char *v = std::getenv(n);
            std::printf("%s=%s\n", n, v ? v : "<unset>");
        }
        return 0;
    }
    QGuiApplication app(argc, argv);
    std::printf("platform=%s\n", qPrintable(QGuiApplication::platformName()));
    std::fflush(stdout);
    return 0;
}
