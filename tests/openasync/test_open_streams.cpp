// app.open_streams — THE OPEN STREAMS IN: NO PLATE (BISTRO-STREAM-IN-1 stage 2). The script's header
// (e2e_open_streams.js.in) states the claims; this driver adds the one no script can make: what is ON THE SCREEN. It
// starts the app on the rig's display (DISPLAY, a WM-less Xvfb whose root holds the app's window at its origin), reads
// the script's lines, and the moment the script says the open returned it photographs the X root — the swapchain's
// presented image, which is what the owner sees and what a held frame never reaches — and crops it to the viewport.
//
// THE BAR (a law of the picture, not an old number): a plate is ONE colour — its luminance spread is the HUD's few
// glyphs over a flat fill — and a frame that draws the scene has its geometry's spread. kPlateSpread sits between them,
// measured on both arms (the driver prints the spread every run): the base's held plate read 0.0xx, the tip's first
// frame of Showroom 2 0.2xx (spikes/bistro-stream-in-1/RESUME.md, the row's table).
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QStringList>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// The photograph must be of the frames the open returned on: at most this many frames later in the script's timeline.
static constexpr long long kGrabFrames = 3;
/// A plate's luminance spread (0..1) is its HUD's glyphs over one fill; the scene's is its geometry's.
static constexpr double kPlateSpread = 0.05;

static double luminanceSpread(const QImage &img)
{
    double sum = 0.0, sum2 = 0.0;
    const qint64 n = qint64(img.width()) * img.height();
    if (n == 0) return 0.0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = img.pixel(x, y);
            const double l = (0.2126 * qRed(c) + 0.7152 * qGreen(c) + 0.0722 * qBlue(c)) / 255.0;
            sum += l;
            sum2 += l * l;
        }
    const double mean = sum / double(n);
    return std::sqrt(std::max(0.0, sum2 / double(n) - mean * mean));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "xcb");   // the root of the rig's X display, as the app's own platform
    QGuiApplication qapp(argc, argv);
    std::printf("== app.open_streams: a cold first open from the Desktop presents the scene, never a plate\n");
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) { std::printf("FAIL: no screen on DISPLAY\n"); return 1; }

    QProcess app;
    app.setProcessChannelMode(QProcess::MergedChannels);
    app.start(QStringLiteral(JAHSHAKA_BINARY),
              { QStringLiteral("--script"), QStringLiteral(OPEN_STREAMS_SCRIPT), QStringLiteral("--script-live") });
    if (!app.waitForStarted(30000)) { std::printf("FAIL: the app did not start\n"); return 1; }

    QByteArray pending;
    QStringList lines;
    bool grabbed = false;
    long long grabAt = 0, openedAt = 0, f0 = -1;
    QImage shot;
    int vx = 0, vy = 0, vw = 0, vh = 0;
    const auto drain = [&]() {
        pending += app.readAllStandardOutput();
        int nl;
        while ((nl = pending.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(pending.left(nl)).trimmed();
            pending.remove(0, nl + 1);
            lines << line;
            if (!grabbed && line.startsWith(QStringLiteral("OPEN_STREAMS opened "))) {
                // THE PHOTOGRAPH FIRST, then the bookkeeping: the root as the X server holds it right now.
                shot = screen->grabWindow(0).toImage();
                grabAt = QDateTime::currentMSecsSinceEpoch();
                grabbed = true;
                const QStringList w = line.split(QLatin1Char(' '));
                if (w.size() >= 8) {
                    openedAt = w[2].toLongLong();
                    f0 = w[3].toLongLong();
                    vx = w[4].toInt(); vy = w[5].toInt(); vw = w[6].toInt(); vh = w[7].toInt();
                }
            }
        }
    };
    // FRAMES BOUND THE SCRIPT (it quits within its own frame budget); this wait is the harness's hang guard only.
    while (app.state() != QProcess::NotRunning) {
        if (app.waitForReadyRead(5)) drain();
    }
    app.waitForFinished(1000);
    drain();
    for (const QString &l : lines)
        if (l.startsWith(QStringLiteral("ok:")) || l.startsWith(QStringLiteral("FAIL:")) ||
            l.startsWith(QStringLiteral("OPEN_STREAMS script")))
            std::printf("    app| %s\n", qPrintable(l));
    int scriptFails = 0;
    bool scriptPass = false;
    QString timeline;
    for (const QString &l : lines) {
        if (l.startsWith(QStringLiteral("FAIL:"))) ++scriptFails;
        if (l == QStringLiteral("OPEN_STREAMS script PASS")) scriptPass = true;
        if (l.startsWith(QStringLiteral("OPEN_STREAMS timeline "))) timeline = l.mid(22);
    }
    CHECK(app.exitStatus() == QProcess::NormalExit, "the app exited normally (code %d)", app.exitCode());
    CHECK(scriptPass && scriptFails == 0, "the script's claims hold (%d failed)", scriptFails);
    CHECK(grabbed && vw > 0 && vh > 0, "the screen was photographed when the open returned (viewport %dx%d at %d,%d)", vw,
          vh, vx, vy);
    if (!grabbed || vw <= 0 || vh <= 0) return 1;

    // WHERE THE PHOTOGRAPH FALLS in the script's frame timeline ([ms, rendered] pairs, one a frame).
    // (a photograph taken before the script's first sample is of the frame the open returned on)
    long long grabFrame = grabAt >= openedAt ? f0 : -1;
    {
        const QStringList pairs = timeline.mid(2, timeline.size() - 4).split(QStringLiteral("],["));
        for (const QString &p : pairs) {
            const QStringList v = p.split(QLatin1Char(','));
            if (v.size() != 2) continue;
            if (v[0].toLongLong() <= grabAt) grabFrame = v[1].toLongLong();
        }
    }
    std::printf("    the open returned at frame %lld (+0 ms); the photograph at frame %lld (+%lld ms)\n", f0, grabFrame,
                grabAt - openedAt);
    CHECK(grabFrame >= f0 && grabFrame - f0 <= kGrabFrames,
          "the photograph is of the frames the open returned on (%lld frames after it, bar %lld)",
          grabFrame - f0, kGrabFrames);

    const QImage view = shot.copy(vx, vy, vw, vh);
    const double spread = luminanceSpread(view);
    const QString png = QStringLiteral("open_streams_first_frame.png");
    view.save(png);
    std::printf("    the viewport's luminance spread %.4f (a plate's is its HUD over one fill; bar %.2f) -> %s\n", spread,
                kPlateSpread, qPrintable(png));
    CHECK(spread > kPlateSpread, "the first presented frame after the open draws the scene, not a plate (spread %.4f)",
          spread);
    std::printf(failures ? "app.open_streams: FAILED\n" : "app.open_streams: PASS\n");
    return failures ? 1 : 0;
}
