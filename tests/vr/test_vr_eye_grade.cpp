// vr.eye_grade's READER — THE COLOUR CONTRACT, JUDGED ON THE PICTURES (lane
// EYE-GRADE-1; SPECS/VR_SPEC.md §6).
//
// Split from the app for the reason perf.capture_bundle and vr.frame_budget are
// split: the scripting engine has no file access, so the APP writes pictures
// (its own eye, its own desktop screenshot) and the RUNNER captures the
// runtime's compositor window, and something has to open the three of them and
// compare. That is this, and it is Qt and QImage and nothing else.
//
// WHAT IT ASSERTS, and each one is a defect the #50 smoke measured:
//
//   (a) THE RUNTIME DISPLAYS OUR BYTES, ONCE. The eye target's bytes are
//       display-encoded (the chain writes a display-referred picture into a
//       UNORM target), so with an _SRGB swapchain — which tells the runtime
//       exactly that — the runtime decodes them and re-encodes them for the
//       display: an identity round trip. The patch the runtime shows must
//       therefore equal the patch our own eye picture holds. With the old UNORM
//       swapchain the runtime read our bytes as LINEAR and encoded them a
//       second time: 28 -> 3, 79 -> 20, 95 -> 29, 99 -> 32 on the rig, i.e. the
//       wearer saw a picture several stops too dark. The tolerance is 2/255 and
//       the sRGB-decode of the same patch is REPORTED beside it, so a failure
//       says which of the two contracts the frame is under rather than only
//       that it is wrong.
//
//   (b) THE EYE IS GRADED LIKE THE DESKTOP. Same scene, same project, same
//       grade: the sky patch's mean log luminance must agree within 0.1 stop.
//       Before this lane the session rendered its own hand-written PostFxDesc.
//
//   (c) THE SWEEP MOVED THE EYE. The exposure arms' centre values must climb.
//
// EVERY MEASUREMENT IS A PATCH OF FLAT SKY, on purpose: a simulated HMD sways
// and turns, so the eye and the desktop camera never look in exactly the same
// direction — but a flat colour sky is the same radiance in every direction, so
// a patch of it compares the GRADE and nothing else. Uniformity is asserted
// rather than assumed: a patch that is not flat means the fixture moved, and
// the reader says so instead of comparing two different things.

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QStringList>
#include <cmath>
#include <cstdio>

namespace {

int gFailures = 0;

void check(bool ok, const QString &what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", qPrintable(what));
    if (!ok) ++gFailures;
}

struct Patch {
    double r = 0, g = 0, b = 0;   ///< mean bytes, 0..255
    int spread = 0;               ///< the widest max-min of any channel in the patch
    bool valid = false;
};

/// A square patch's mean, in BYTES — the pictures are display-encoded, so the
/// bytes are the thing both sides of every comparison are in.
Patch patchAt(const QImage &img, double fx, double fy, int half)
{
    Patch p;
    if (img.isNull()) return p;
    const int cx = int(fx * img.width()), cy = int(fy * img.height());
    long long sr = 0, sg = 0, sb = 0;
    int n = 0, lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
    for (int y = cy - half; y <= cy + half; ++y) {
        if (y < 0 || y >= img.height()) continue;
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x < 0 || x >= img.width()) continue;
            const QRgb c = img.pixel(x, y);
            const int v[3] = { qRed(c), qGreen(c), qBlue(c) };
            sr += v[0]; sg += v[1]; sb += v[2]; ++n;
            for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
        }
    }
    if (!n) return p;
    p.r = double(sr) / n; p.g = double(sg) / n; p.b = double(sb) / n;
    for (int k = 0; k < 3; ++k) p.spread = std::max(p.spread, hi[k] - lo[k]);
    p.valid = true;
    return p;
}

double srgbToLinear(double byteValue)
{
    const double c = byteValue / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double linearToSrgbByte(double linear)
{
    const double c = linear <= 0.0031308 ? linear * 12.92
                                         : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    return std::min(255.0, std::max(0.0, c * 255.0));
}

/// Rec.709 luminance of a display-encoded patch, in LINEAR light — which is the
/// only space in which a difference may be called "a stop".
double luminance(const Patch &p)
{
    return 0.2126 * srgbToLinear(p.r) + 0.7152 * srgbToLinear(p.g) + 0.0722 * srgbToLinear(p.b);
}

QImage load(const QDir &dir, const QString &name)
{
    QImage img(dir.filePath(name));
    if (img.isNull()) return img;
    return img.convertToFormat(QImage::Format_RGB32);
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { std::printf("usage: test_vr_eye_grade <outdir>\n"); return 2; }
    const QDir dir(QString::fromLocal8Bit(argv[1]));
    std::printf("vr.eye_grade reader: %s\n", qPrintable(dir.absolutePath()));

    // WHERE THE PATCHES ARE. The runtime's window carries BOTH eyes side by
    // side, so the left eye's patch sits at (0.25, 0.25) of the window and at
    // (0.5, 0.25) of our own single-eye picture. A quarter of the way down is
    // SKY in both, by different routes: the wearer's head is level (the runtime
    // decides that) and the sky fills the upper part of that view, while the
    // script deliberately points the EDITOR camera up at the zenith so its
    // whole frame is sky. That asymmetry is the point — a flat colour sky is
    // the same radiance in every direction, so two pictures looking different
    // ways still compare the GRADE and nothing else.
    const double kEyeX = 0.50, kRuntimeX = 0.25, kY = 0.25;
    const int kHalf = 12;                 // a 25x25 patch
    const double kBytesTolerance = 2.0;   // (a)
    const double kStopsTolerance = 0.1;   // (b)
    const int kFlatSpread = 6;            // what still counts as a flat patch

    int arms = 0;
    QList<double> eyeMeans;
    for (int a = 0; a < 8; ++a) {
        const QString eyeName = QStringLiteral("eye-%1.png").arg(a);
        if (!QFile::exists(dir.filePath(eyeName))) continue;
        ++arms;
        const QImage eye = load(dir, eyeName);
        const QImage desk = load(dir, QStringLiteral("desk-%1.png").arg(a));
        const QImage rt = load(dir, QStringLiteral("runtime-%1.png").arg(a));
        check(!eye.isNull(), QStringLiteral("arm %1: the eye's own picture opened").arg(a));
        check(!desk.isNull(), QStringLiteral("arm %1: the desktop's picture opened").arg(a));
        check(!rt.isNull(),
              QStringLiteral("arm %1: the RUNTIME's window was captured (the compositor "
                             "window is what the wearer's headset is shown)").arg(a));
        if (eye.isNull() || desk.isNull() || rt.isNull()) continue;

        const Patch pe = patchAt(eye, kEyeX, kY, kHalf);
        const Patch pd = patchAt(desk, kEyeX, kY, kHalf);
        const Patch pr = patchAt(rt, kRuntimeX, kY, kHalf);
        check(pe.valid && pd.valid && pr.valid, QStringLiteral("arm %1: all three patches read").arg(a));
        if (!pe.valid || !pd.valid || !pr.valid) continue;
        eyeMeans.append(pe.g);

        std::printf("    arm %d  eye %.1f,%.1f,%.1f (spread %d)  desktop %.1f,%.1f,%.1f  "
                    "runtime %.1f,%.1f,%.1f (spread %d)\n",
                    a, pe.r, pe.g, pe.b, pe.spread, pd.r, pd.g, pd.b, pr.r, pr.g, pr.b, pr.spread);

        check(pe.spread <= kFlatSpread && pr.spread <= kFlatSpread && pd.spread <= kFlatSpread,
              QStringLiteral("arm %1: the sky patch is flat in all three pictures (spreads "
                             "%2 eye, %3 runtime, %4 desktop)")
                  .arg(a).arg(pe.spread).arg(pr.spread).arg(pd.spread));

        // ---- (a) the colour contract -------------------------------------
        const double dr = std::fabs(pr.r - pe.r), dg = std::fabs(pr.g - pe.g),
                     db = std::fabs(pr.b - pe.b);
        const double worst = std::max(dr, std::max(dg, db));
        // What the OLD contract would have shown, for the failure message: the
        // runtime encoding our already-encoded bytes a second time.
        const double doubleEncoded = linearToSrgbByte(pe.g / 255.0);
        check(worst <= kBytesTolerance,
              QStringLiteral("arm %1: the runtime displays our bytes ONCE — worst channel "
                             "%2/255 (a second encode would have read %3 against our %4)")
                  .arg(a).arg(worst, 0, 'f', 2).arg(doubleEncoded, 0, 'f', 1)
                  .arg(pe.g, 0, 'f', 1));

        // ---- (b) the eye is graded like the desktop ------------------------
        const double le = luminance(pe), ld = luminance(pd);
        // TWO BLACK PATCHES AGREE. A ratio of logs cannot say so — log2(0/0) is
        // not a number — and a sky dark enough to clip to code 0 is a legitimate
        // arm, so the zero case is answered before the division rather than
        // reported as an infinite disagreement (which is what the first cut of
        // this reader did).
        const double kBlack = 1e-6;
        const double stops = (le <= kBlack && ld <= kBlack) ? 0.0
                             : (le > 0.0 && ld > 0.0)       ? std::log2(le / ld)
                                                            : 99.0;
        check(std::fabs(stops) <= kStopsTolerance,
              QStringLiteral("arm %1: the eye is graded like the desktop — %2 stops apart "
                             "(eye %3, desktop %4 in linear light)")
                  .arg(a).arg(stops, 0, 'f', 4).arg(le, 0, 'f', 5).arg(ld, 0, 'f', 5));
    }
    check(arms >= 3, QStringLiteral("at least three sky arms were written (%1)").arg(arms));

    // The fixture really moved: a brighter sky is a brighter eye.
    bool climbs = eyeMeans.size() >= 3;
    for (int i = 1; i < eyeMeans.size(); ++i) climbs = climbs && eyeMeans[i] > eyeMeans[i - 1];
    check(climbs, QStringLiteral("the eye follows the sky level across the arms"));

    // ---- (c) the exposure sweep reached the eye's pixels -------------------
    QList<double> sweep;
    for (int s = 0; s < 8; ++s) {
        const QString name = QStringLiteral("sweep-%1.png").arg(s);
        if (!QFile::exists(dir.filePath(name))) continue;
        const QImage img = load(dir, name);
        const Patch p = patchAt(img, kEyeX, kY, kHalf);
        if (p.valid) { sweep.append(p.g); std::printf("    sweep %d  eye %.1f\n", s, p.g); }
    }
    bool sweepClimbs = sweep.size() >= 3;
    for (int i = 1; i < sweep.size(); ++i) sweepClimbs = sweepClimbs && sweep[i] > sweep[i - 1];
    check(sweepClimbs,
          QStringLiteral("the exposure sweep moved the eye's own pixels (%1 arms)").arg(sweep.size()));

    // ---- the mid-session grade reached the RUNTIME's own picture -----------
    //
    // The flat arms above are all at the default exposure, which a session
    // could in principle have baked when it was created. This one was taken
    // after `world.postFx({exposureEv:-2})` was called with the wearer already
    // in there, so it is the arm that says the World panel reaches the picture
    // the RUNTIME displays and not merely the mono control.
    if (QFile::exists(dir.filePath(QStringLiteral("eye-sweeplow.png")))) {
        const QImage eye = load(dir, QStringLiteral("eye-sweeplow.png"));
        const QImage desk = load(dir, QStringLiteral("desk-sweeplow.png"));
        const QImage rt = load(dir, QStringLiteral("runtime-sweeplow.png"));
        check(!eye.isNull() && !desk.isNull() && !rt.isNull(),
              QStringLiteral("the mid-session grade's three pictures opened"));
        if (!eye.isNull() && !desk.isNull() && !rt.isNull()) {
            const Patch pe = patchAt(eye, kEyeX, kY, kHalf);
            const Patch pd = patchAt(desk, kEyeX, kY, kHalf);
            const Patch pr = patchAt(rt, kRuntimeX, kY, kHalf);
            std::printf("    mid-session (-2 EV)  eye %.1f,%.1f,%.1f  desktop %.1f,%.1f,%.1f  "
                        "runtime %.1f,%.1f,%.1f\n",
                        pe.r, pe.g, pe.b, pd.r, pd.g, pd.b, pr.r, pr.g, pr.b);
            const double worst = std::max({ std::fabs(pr.r - pe.r), std::fabs(pr.g - pe.g),
                                            std::fabs(pr.b - pe.b) });
            check(worst <= kBytesTolerance,
                  QStringLiteral("a grade set MID-SESSION reaches the runtime's own picture "
                                 "(worst channel %1/255)").arg(worst, 0, 'f', 2));
            const double le = luminance(pe), ld = luminance(pd);
            const double stops = (le > 1e-6 && ld > 1e-6) ? std::log2(le / ld) : 0.0;
            check(std::fabs(stops) <= kStopsTolerance,
                  QStringLiteral("...and the eye is still graded like the desktop there "
                                 "(%1 stops)").arg(stops, 0, 'f', 4));
        }
    }

    // ---- the measurement override's own path ------------------------------
    //
    // With `vr.begin({eyeWidth, eyeHeight})` the eye is SCALED into the
    // runtime's swapchain instead of copied, and a scale is a blit — which
    // CONVERTS. Straight into an _SRGB swapchain that is a second encode (about
    // a stop too bright) on the one arm whose job is to be comparable with the
    // product path; the scale now happens between two UNORM images and the raw
    // copy does the rest, so this must read what every other arm reads.
    if (QFile::exists(dir.filePath(QStringLiteral("eye-override.png")))) {
        const QImage eye = load(dir, QStringLiteral("eye-override.png"));
        const QImage rt = load(dir, QStringLiteral("runtime-override.png"));
        check(!eye.isNull() && !rt.isNull(),
              QStringLiteral("the overridden-size arm's pictures opened"));
        if (!eye.isNull() && !rt.isNull()) {
            const Patch pe = patchAt(eye, kEyeX, kY, kHalf);
            const Patch pr = patchAt(rt, kRuntimeX, kY, kHalf);
            std::printf("    override (%dx%d)  eye %.1f,%.1f,%.1f (spread %d)  "
                        "runtime %.1f,%.1f,%.1f (spread %d)\n",
                        eye.width(), eye.height(), pe.r, pe.g, pe.b, pe.spread,
                        pr.r, pr.g, pr.b, pr.spread);
            const double worst = std::max({ std::fabs(pr.r - pe.r), std::fabs(pr.g - pe.g),
                                            std::fabs(pr.b - pe.b) });
            check(pe.spread <= kFlatSpread && pr.spread <= kFlatSpread,
                  QStringLiteral("the overridden arm's patch is flat in both pictures "
                                 "(spreads %1 and %2) — it has to be: the eye render and "
                                 "the window capture are 600 frames apart and the "
                                 "simulated head moves through them")
                      .arg(pe.spread).arg(pr.spread));
            // The eye is SCALED here, so the tolerance is the flat arms' plus
            // what a linear filter can do to a flat patch (nothing, in theory;
            // 2/255 in practice is still far below a second encode's tens).
            check(worst <= kBytesTolerance,
                  QStringLiteral("the SCALED eye reaches the runtime with one encode "
                                 "(worst channel %1/255; a blit into the sRGB swapchain "
                                 "would have read %2 against our %3)")
                      .arg(worst, 0, 'f', 2)
                      .arg(linearToSrgbByte(pe.g / 255.0), 0, 'f', 1).arg(pe.g, 0, 'f', 1));
        }
    }

    // ---- the realistic-sky arm: a picture, and the runtime showing it ------
    if (QFile::exists(dir.filePath(QStringLiteral("eye-realistic.png")))) {
        const QImage eye = load(dir, QStringLiteral("eye-realistic.png"));
        const QImage rt = load(dir, QStringLiteral("runtime-realistic.png"));
        check(!eye.isNull() && !rt.isNull(),
              QStringLiteral("the realistic sky's eye picture and the runtime's window both opened"));
        if (!eye.isNull() && !rt.isNull()) {
            const Patch pe = patchAt(eye, kEyeX, kY, kHalf);
            const Patch pr = patchAt(rt, kRuntimeX, kY, kHalf);
            std::printf("    realistic  eye %.1f,%.1f,%.1f  runtime %.1f,%.1f,%.1f\n",
                        pe.r, pe.g, pe.b, pr.r, pr.g, pr.b);
            // A GRADIENT, so the tolerance is wider than the flat arms' 2/255:
            // the compositor's sampling of a sky that changes with altitude
            // cannot be byte-exact against ours. Six is still far below the
            // double encode, which at these levels is tens of codes.
            const double worst = std::max({ std::fabs(pr.r - pe.r), std::fabs(pr.g - pe.g),
                                            std::fabs(pr.b - pe.b) });
            check(worst <= 6.0,
                  QStringLiteral("under the realistic sky the runtime still displays our bytes "
                                 "once (worst channel %1/255; a second encode would read %2 "
                                 "against our %3)")
                      .arg(worst, 0, 'f', 2)
                      .arg(linearToSrgbByte(pe.g / 255.0), 0, 'f', 1).arg(pe.g, 0, 'f', 1));
        }
    }

    std::printf("%s\n", gFailures ? "vr.eye_grade reader: FAILED" : "vr.eye_grade reader: all ok");
    return gFailures ? 1 : 0;
}
