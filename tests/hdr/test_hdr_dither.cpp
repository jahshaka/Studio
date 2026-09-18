// THE GRADE'S 8-BIT WRITE IS DITHERED (lane DITHER-1, ogre-patch 0079).
//
// THE DEFECT. The renderer computes in RGBA16F and every target a person's eye
// ever sees is 8-bit UNORM — the window, the offscreen render target, the VR
// eye image. A smooth shading gradient therefore lands on a staircase of
// display codes, and a staircase that slides with the camera is what the owner
// reported on 2026-09-18 as "faint circular rippling in the ground plane while
// flying" (ledger §768: concentric contours of 1-2/255 on a plain-colour
// ground, 3-5 % of pixels shifting per 0.5 m of camera motion). There was no
// dither anywhere in the final grade. Now there is one, in the tonemap quad,
// which is THE place a float picture becomes display codes in this engine.
//
// THE FIXTURE is the defect's own shape and nothing else: a large matte plane
// filling the frame, square on to the camera, lit by ONE point light on the
// view axis. The falloff is a smooth radial gradient of TEN display codes over
// the frame — the concentric contours the owner sees, with no geometry, no
// texture and no GI in the picture to argue about. Ten codes over 256 pixels
// is the SHALLOW gradient banding needs: a steep ramp crosses a code every
// pixel or two and cannot band.
//
// EVERY ARM IS MEASURED AGAINST THE SAME BINARY WITH THE DITHER OFF, in ONE
// process: JAHSHAKA_NO_DITHER is read where the uniform is pushed, so the test
// flips it between two renders (chain::setDither). That is what makes the
// discrimination arms real — a suite that only ever sees the dithered picture
// cannot tell a working dither from a broken assertion.
//
// THE ARMS
//   A  THE BANDING SIGNATURE. Along a cut through the gradient, the UNDITHERED
//      picture holds long runs of one identical value (a band); the dithered
//      one must not. This is the assertion that fails if the dither is removed,
//      stops being applied, or is applied on the wrong axis.
//   B  IT IS ZERO-MEAN. The frame's mean must not move by more than 0.02 of a
//      code. A dither that brightens or darkens the picture is a bug, not a
//      dither.
//   C  ITS AMPLITUDE IS AT MOST ONE CODE. No pixel may differ from its
//      undithered self by more than 1/255 — the dither perturbs the rounding,
//      never the picture.
//   D  IT IS DETERMINISTIC. Two renders of one still frame are byte-identical,
//      which every pixel suite, every thumbnail and the --engine-selftest hash
//      depend on.
//   E  THE LOCAL MEAN DID NOT MOVE. B is the whole frame; this is every 9x9
//      neighbourhood on a cut, to within half a code. A dither correlated with
//      the gradient would pass B and still repaint the picture.
//   F  THE STAIRCASE IS GONE. The undithered picture's 9x9 local mean sits DEAD
//      STILL for stretches of columns and then jumps — that is the band, on the
//      picture the eye integrates. The dithered one must never sit still.
//   G  ONE NOISE VALUE FOR ALL THREE CHANNELS: a grey world stays exactly grey.
//   H  THE ON-CODE IDENTITY, arithmetically (no picture can exhibit it).
//   I  THE LDR BUFFER SMAA WORKS ON DOES NOT QUANTISE THE PICTURE A SECOND
//      TIME — the lane's other defect, which has nothing to do with the dither
//      and needs a chain WITH SMAA to be visible at all.
//   J  THE OFFSET PATTERN ITSELF, on an UNSHADED frame: the one arm whose
//      numbers depend on the noise and on nothing a driver's float arithmetic
//      decides, and therefore the one to compare across drivers by hand.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                           std::printf("\n"); ++failures; } \
                           else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {

constexpr unsigned kSide = 256;        // square: the gradient is radial
Engine *gEngine = nullptr;
View   *gView = nullptr;

/// A flat quad in the XY plane at z == 0, `half` world units either side of the
/// origin, normals facing +Z. Two triangles: the fixture must have no shading
/// structure of its own, so a subdivided plane would only add interpolation
/// artefacts. It faces the camera rather than lying in XZ so that the camera
/// keeps its IDENTITY orientation — a look-at straight down the +Y axis with a
/// +Y up vector is degenerate and renders nothing at all.
MeshData planeMesh(float half)
{
    MeshData d;
    const float z = 0.0f;
    const float p[4][3] = { {-half, -half, z}, { half, -half, z},
                            { half,  half, z}, {-half,  half, z} };
    for (int i = 0; i < 4; ++i) {
        d.positions.insert(d.positions.end(), { p[i][0], p[i][1], p[i][2] });
        d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
    }
    d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
}

/// Flips the DIAGNOSTIC off switch on this view's description — the hook that
/// makes every arm below an A/B of one binary against itself, in one process,
/// with no environment variable and no other thread involved.
void setDither(bool off)
{
    PostFxDesc fx = gView->postFx();
    fx.ditherOff = off;
    gView->setPostFx(fx);
}

/// Renders `frames` and reads the target back. The picture is still, so the
/// count only has to cover the mirror/chain settling, not a wall clock.
bool shoot(Image &out, int frames = 4)
{
    for (int i = 0; i < frames; ++i) gEngine->renderOneFrame();
    return gView->readPixels(out);
}

/// The green channel as a flat vector of codes.
std::vector<int> green(const Image &img)
{
    std::vector<int> v(size_t(img.width) * img.height);
    for (size_t i = 0; i < v.size(); ++i) v[i] = img.rgba[i * 4 + 1];
    return v;
}

/// The longest run of identical values along one row — the band's own length.
int longestRun(const std::vector<int> &g, unsigned w, unsigned row)
{
    int best = 1, cur = 1;
    for (unsigned x = 1; x < w; ++x) {
        cur = (g[size_t(row) * w + x] == g[size_t(row) * w + x - 1]) ? cur + 1 : 1;
        if (cur > best) best = cur;
    }
    return best;
}

/// A (2r+1)^2 box mean, at float precision, of one row's neighbourhood.
std::vector<double> boxMeanRow(const std::vector<int> &g, unsigned w, unsigned h,
                               unsigned row, int r)
{
    std::vector<double> out(w, 0.0);
    for (unsigned x = 0; x < w; ++x) {
        double sum = 0.0; int n = 0;
        for (int dy = -r; dy <= r; ++dy) {
            const int yy = std::min(std::max(int(row) + dy, 0), int(h) - 1);
            for (int dx = -r; dx <= r; ++dx) {
                const int xx = std::min(std::max(int(x) + dx, 0), int(w) - 1);
                sum += g[size_t(yy) * w + xx]; ++n;
            }
        }
        out[x] = sum / n;
    }
    return out;
}

double maxStep(const std::vector<double> &v)
{
    double m = 0.0;
    for (size_t i = 1; i < v.size(); ++i) m = std::max(m, std::fabs(v[i] - v[i - 1]));
    return m;
}

double meanOf(const std::vector<int> &g)
{
    double s = 0.0;
    for (int v : g) s += v;
    return s / double(g.size());
}

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-hdr-dither-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    gEngine = engine.get();

    gView = engine->createOffscreenView("dither", kSide, kSide, Colour(0, 0, 0));
    Scene *s = engine->createScene("dither");
    if (!gView || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    gView->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    gView->setShadows(false);

    // The plane: matte, mid grey, no metal. Its own albedo carries no structure,
    // so every code in the picture comes from the light's falloff.
    {
        const NodeId n = s->createNode();
        const MeshId m = s->createMesh(planeMesh(40.0f));
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        p.metalness = 0.0f;
        p.roughness = 1.0f;
        const MaterialId mat = s->createPbrMaterial(p);
        if (!n || !m || !mat || !s->attachMesh(n, m, mat)) {
            std::printf("FAIL: fixture plane: %s\n", engine->lastError().c_str());
            return 1;
        }
    }
    // ONE point light, far enough in front of the plane that its 1/r^2 falloff
    // spreads a gentle gradient across the whole frame rather than a hot spot.
    {
        const NodeId n = s->createNode();
        // 32 units in front of the plane, 4 units behind the camera's own
        // distance: measured, so that the falloff spreads TEN display codes
        // across the frame. That is the owner's case — a gradient shallow
        // enough that one code is tens of pixels wide — and not a steep ramp,
        // which cannot band at all.
        s->setNodeTransform(n, Vec3{0.0f, 0.0f, 32.0f}, Quat{}, Vec3{1, 1, 1});
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 2.0f;        // measured: the frame lands on codes 134..144
        l.range = 2000.0f;         // no range cut-off inside the fixture
        l.castShadows = false;
        if (!s->setLight(n, l)) { std::printf("FAIL: fixture light\n"); return 1; }
    }
    // Square on to the plane's centre, identity orientation (looking down -Z):
    // the gradient is radial about the image centre and fills the frame.
    enginetest::testCameraAt(gView, Vec3(0.0f, 0.0f, 28.0f));

    // THE GRADE. hdr + a FIXED exposure, which is what a screenshot, a
    // thumbnail and the editor's own picture all use; the dither lives in that
    // quad's write.
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdr = true;
        fx.tonemapFixed = true;
        fx.exposure = 0.0f;
        gView->setPostFx(fx);
    }

    // ---- the two pictures, one process ------------------------------------
    Image dithered, plain, again;
    if (!shoot(dithered, 8)) { std::printf("FAIL: readPixels (dithered)\n"); return 1; }
    if (!shoot(again, 2))    { std::printf("FAIL: readPixels (repeat)\n"); return 1; }
    setDither(true);
    if (!shoot(plain, 2))    { std::printf("FAIL: readPixels (undithered)\n"); return 1; }
    setDither(false);

    const std::vector<int> gd = green(dithered), gp = green(plain), ga = green(again);
    const unsigned w = dithered.width, h = dithered.height;

    // THE FIXTURE IS A GRADIENT AT ALL — the control. Without this, every arm
    // below could pass on a flat picture.
    int lo = 255, hi = 0;
    for (int v : gp) { lo = std::min(lo, v); hi = std::max(hi, v); }
    CHECK(hi - lo >= 8, "the fixture renders a gradient of %d codes (%d..%d)", hi - lo, lo, hi);

    // ---- A  THE BANDING SIGNATURE ----------------------------------------
    {
        const unsigned row = h / 2;
        const int runPlain = longestRun(gp, w, row);
        const int runDith  = longestRun(gd, w, row);
        CHECK(runPlain >= 48,
              "A control: WITHOUT the dither the centre row holds a run of %d identical "
              "codes (a band)", runPlain);
        CHECK(runDith <= 32 && runDith * 3 <= runPlain,
              "A: WITH the dither the longest identical run on that row is %d px "
              "(was %d) — the band is noise now", runDith, runPlain);
    }

    // ---- B  ZERO MEAN ------------------------------------------------------
    {
        const double md = meanOf(gd), mp = meanOf(gp);
        CHECK(std::fabs(md - mp) <= 0.02,
              "B: the dither does not move the picture's mean (%.5f vs %.5f, "
              "difference %.5f codes)", md, mp, std::fabs(md - mp));
    }

    // ---- C  AT MOST ONE CODE ----------------------------------------------
    {
        int worst = 0; size_t moved = 0;
        for (size_t i = 0; i < gd.size(); ++i) {
            const int d = std::abs(gd[i] - gp[i]);
            worst = std::max(worst, d);
            if (d) ++moved;
        }
        CHECK(worst <= 1,
              "C: no pixel moves by more than one code (worst %d; %zu of %zu pixels moved "
              "by exactly 1)", worst, moved, gd.size());
        CHECK(moved > gd.size() / 20,
              "C control: the dither actually reaches the picture (%zu of %zu pixels)",
              moved, gd.size());
    }

    // ---- D  DETERMINISM, ON EVERY CHANNEL ----------------------------------
    {
        const bool same = again.rgba == dithered.rgba;
        CHECK(same, "D: two renders of one still frame are identical on every channel "
                    "(the dither has no time term)%s", same ? "" : " — they are not");
    }

    // ---- G  ONE NOISE VALUE FOR ALL THREE CHANNELS -------------------------
    // The fixture is grey by construction — a white light on a grey albedo —
    // so a picture that stays grey is the whole claim: the offset is one
    // scalar, added to all three channels, which is what keeps the residual
    // luminance noise instead of colour speckle.
    {
        size_t coloured = 0;
        for (size_t i = 0; i < gd.size(); ++i) {
            const unsigned char *px = &dithered.rgba[i * 4];
            if (px[0] != px[1] || px[1] != px[2]) ++coloured;
        }
        CHECK(coloured == 0,
              "G: the dithered picture of a grey world is still exactly grey on every pixel "
              "(R == G == B; %zu pixels were not)", coloured);
    }

    // ---- H  THE ON-CODE IDENTITY -------------------------------------------
    // A VALUE ALREADY SITTING EXACTLY ON A CODE MUST ROUND BACK TO IT. That is
    // what lets a dithered write sit in front of a picture somebody else has
    // already quantised without moving it, and it is why the amplitude is
    // 0.498 of a code rather than 0.500.
    //
    // NO RENDERED PICTURE CAN EXHIBIT IT, which is why this arm is arithmetic:
    // the tonemapper's output never lands exactly on a code except by
    // coincidence, and there is no second dithered write in the chain today to
    // feed an already-quantised picture through. So the shader's own rule is
    // restated here — floor(k + 0.5 + n), at the extremes of n — over every
    // code. This is the ONE place the amplitude is written twice; it exists to
    // catch exactly the change that would break the property, somebody raising
    // 0.996 to 1.0 (JahDither.glsl).
    {
        const double peak = 0.5 * 0.996;        // the shader's amplitude, in codes
        int wrong = 0;
        for (int k = 0; k <= 255; ++k)
            for (double n : { -peak, peak })
                if (int(std::floor(double(k) + 0.5 + n)) != k) ++wrong;
        CHECK(wrong == 0,
              "H: floor(k + 0.5 + n) == k for every code and both extremes of the dither's "
              "+-%.3f (%d violations) — an already quantised picture survives this write",
              peak, wrong);
    }

    // ---- E  THE LOCAL MEAN DID NOT MOVE ------------------------------------
    // B says the dither is zero-mean over the whole frame; this says it is
    // zero-mean LOCALLY, which is the property that matters — a dither whose
    // noise correlated with the gradient would pass B and still repaint the
    // picture. 9x9 is the neighbourhood the eye integrates at arm's length.
    {
        const unsigned row = h / 2;
        const std::vector<double> lmD = boxMeanRow(gd, w, h, row, 4);
        const std::vector<double> lmP = boxMeanRow(gp, w, h, row, 4);
        double worst = 0.0;
        for (unsigned x = 0; x < w; ++x) worst = std::max(worst, std::fabs(lmD[x] - lmP[x]));
        CHECK(worst <= 0.5,
              "E: the 9x9 local mean is unchanged to within half a code everywhere on the "
              "cut (worst %.4f)", worst);
    }

    // ---- F  THE STAIRCASE IS GONE -----------------------------------------
    // THE BANDING SIGNATURE, on the picture the eye actually integrates: a
    // banded gradient's local mean SITS STILL for a stretch of columns and then
    // jumps; a dithered one's climbs every column. Counting the dead-flat
    // stretches is the one number that separates the two without a float
    // reference picture to compare against.
    {
        const unsigned row = h / 2;
        const std::vector<double> lmD = boxMeanRow(gd, w, h, row, 4);
        const std::vector<double> lmP = boxMeanRow(gp, w, h, row, 4);
        auto flatStretches = [](const std::vector<double> &v, int minLen) {
            int n = 0, run = 1;
            for (size_t i = 1; i < v.size(); ++i) {
                if (std::fabs(v[i] - v[i - 1]) < 1e-9) { ++run; }
                else { if (run >= minLen) ++n; run = 1; }
            }
            return run >= minLen ? n + 1 : n;
        };
        // 40 columns: the fixture's bands are ~96 px wide, and a dithered local
        // mean CAN repeat a value by chance (the column entering the 9x9 window
        // and the one leaving it can carry equal sums) but never for 40 in a row.
        const int flatP = flatStretches(lmP, 40), flatD = flatStretches(lmD, 40);
        CHECK(flatP >= 1,
              "F control: WITHOUT the dither the local mean sits dead still for %d stretch(es) "
              "of 40 columns or more (the staircase; the widest band on the cut)", flatP);
        CHECK(flatD == 0,
              "F: WITH the dither it never sits still at all (%d such stretches, was %d)",
              flatD, flatP);
    }

    // ---- I  THE LDR BUFFER DOES NOT QUANTISE THE PICTURE A SECOND TIME ------
    //
    // THE LANE'S OTHER DEFECT, and it is not the dither's: `kLdr`, the buffer
    // the tonemap writes when SMAA is on, was an _SRGB attachment. This
    // engine's tonemapper emits the DISPLAY CODE, so that target encoded it as
    // though it were linear and the sampler decoded it again — the space SMAA
    // sees never changed, because decode(encode(d)) is d, but the 8-bit round
    // trip through a curve the value was never in CANNOT CARRY 73 of the 256
    // codes. They start at 75 (75, 83, 89, 94, 98, 102 …, thinning to every
    // other code only near the top), and this fixture's 134..144 contains four
    // of them — 135, 137, 140 and 143 — which is why it can see this at all.
    //
    // THE TEST, with the dither off so the comparison is exact: the SAME scene
    // rendered with SMAA on and with SMAA off must be BYTE-IDENTICAL. SMAA has
    // no edge to find in a smooth gradient, so its blend is a copy; the only
    // thing its presence changes is whether the picture went through kLdr. It
    // discriminates — verified by hand once, by flipping the format back to
    // _SRGB in OgreChain::build: 73,896 of 262,144 bytes then differ.
    {
        PostFxDesc fx = gView->postFx();
        fx.ditherOff = true;
        fx.smaaPreset = -1;
        gView->setPostFx(fx);
        Image noSmaa;
        if (!shoot(noSmaa, 4)) { std::printf("FAIL: readPixels (SMAA off)\n"); return 1; }
        fx.smaaPreset = 3;                      // Ultra: the tier the editor ships at
        gView->setPostFx(fx);
        Image withSmaa;
        if (!shoot(withSmaa, 6)) { std::printf("FAIL: readPixels (SMAA on)\n"); return 1; }
        fx.smaaPreset = -1; fx.ditherOff = false;
        gView->setPostFx(fx);

        size_t differ = 0; int worst = 0;
        for (size_t i = 0; i < noSmaa.rgba.size(); ++i) {
            const int d = std::abs(int(noSmaa.rgba[i]) - int(withSmaa.rgba[i]));
            if (d) { ++differ; worst = std::max(worst, d); }
        }
        CHECK(differ == 0,
              "I: the LDR buffer SMAA works on is a lossless carrier — the same gradient "
              "renders byte-identically with SMAA on and off (%zu of %zu bytes differ, "
              "worst %d)", differ, noSmaa.rgba.size(), worst);
    }

    // ---- J  THE OFFSET PATTERN IS THE SAME ON EVERY DRIVER ------------------
    //
    // THE ONE ARM THAT TESTS THE NOISE AND NOTHING ELSE. Every arm above reads
    // a SHADED picture, so what it measures is the noise AND the float value
    // the filmic curve produced — and that value is not bit-identical between
    // vendors (measured: NVIDIA and lavapipe disagree on 81 % of the default
    // scene's pixels with the dither off). This arm removes the shading: the
    // scene is emptied to the view's own BACKGROUND, one exact constant that
    // every implementation carries identically, so the only thing left in the
    // difference between the dithered and undithered frames is the dither's
    // own pattern.
    //
    // WHAT IT ASSERTS is the pattern's SHAPE, which a second driver can then be
    // compared against by hand from the same numbers: the fraction of pixels
    // the dither lifts by one code, and a checksum of WHICH ones. Both are
    // pure functions of the integer noise, so a float regression in it — the
    // very thing the integer arithmetic exists to prevent — moves them.
    {
        // A background the dither can move in both directions: mid grey, not
        // black (a code clamped at 0 cannot be dithered down).
        gView->setBackground(Colour(0.35f, 0.35f, 0.35f));
        // The plane is at z == 0 and the camera looks down -Z, so backing off
        // to +500 leaves it far behind: the frame is the clear colour and
        // nothing else. (setScene(nullptr) would take the chain down with it.)
        enginetest::testCameraAt(gView, Vec3(0.0f, 0.0f, -500.0f));
        PostFxDesc fx = gView->postFx();
        fx.ditherOff = true;  gView->setPostFx(fx);
        Image flatPlain;
        if (!shoot(flatPlain, 4)) { std::printf("FAIL: readPixels (flat, undithered)\n"); return 1; }
        fx.ditherOff = false; gView->setPostFx(fx);
        Image flatDith;
        if (!shoot(flatDith, 4)) { std::printf("FAIL: readPixels (flat, dithered)\n"); return 1; }

        // The undithered frame must be ONE code — it is a clear colour.
        bool uniform = true;
        const unsigned char base = flatPlain.rgba[1];
        for (size_t i = 0; i < flatPlain.rgba.size(); i += 4)
            if (flatPlain.rgba[i + 1] != base) { uniform = false; break; }
        CHECK(uniform, "J control: an unshaded frame is one code everywhere (%u)", base);

        size_t lifted = 0, dropped = 0;
        unsigned long long sum = 0;             // a checksum of WHICH pixels moved
        for (size_t p = 0; p < size_t(flatDith.width) * flatDith.height; ++p) {
            const int d = int(flatDith.rgba[p * 4 + 1]) - int(base);
            if (d > 0) { ++lifted; sum = sum * 1000003ull + p; }
            else if (d < 0) { ++dropped; sum = sum * 1000003ull + (p ^ 0xFFFFFFFFull); }
        }
        // JAH_DITHER_DUMP=<file>: the flat dithered frame as a PGM, so the same
        // arm on ANOTHER DRIVER can be compared against this one by hand. The
        // comparison that matters is not equality of the counts — the frame's
        // fractional part is the filmic curve's float output and two drivers
        // disagree on it slightly — but NESTING: both sets are the same noise
        // field thresholded at that fraction, so the smaller must be a SUBSET
        // of the larger if, and only if, the noise field is identical.
        if (const char *dump = std::getenv("JAH_DITHER_DUMP")) {
            if (FILE *f = std::fopen(dump, "wb")) {
                std::fprintf(f, "P5\n%u %u\n255\n", flatDith.width, flatDith.height);
                for (size_t p2 = 0; p2 < size_t(flatDith.width) * flatDith.height; ++p2)
                    std::fwrite(&flatDith.rgba[p2 * 4 + 1], 1, 1, f);
                std::fclose(f);
            }
        }
        const size_t px = size_t(flatDith.width) * flatDith.height;
        std::printf("   [J] flat base code %u, lifted %zu, dropped %zu of %zu, "
                    "pattern checksum %llu\n", base, lifted, dropped, px, sum);
        // The offsets are uniform on (-0.498, 0.498), so the share that moves
        // the code is |f| for the frame's single fraction f — never all of it,
        // never none of it, and the two directions are exclusive.
        CHECK(lifted + dropped > 0 && lifted + dropped < px,
              "J: the dither moves SOME of a flat frame and not all of it "
              "(%zu lifted, %zu dropped, of %zu)", lifted, dropped, px);
        CHECK(lifted == 0 || dropped == 0,
              "J: a flat frame's dither moves its code in ONE direction only "
              "(%zu lifted, %zu dropped)", lifted, dropped);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
