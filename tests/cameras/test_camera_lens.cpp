// THE LENS MATH (CAMERA_LENS_SPEC §3, phases P1 and P2), against hand-computed
// values — pure iris::, no engine, no app, no display.
//
// WHY A GOLDEN TABLE AND NOT A ROUND TRIP. A round trip proves two functions
// are inverses; it cannot notice that BOTH of them are wrong. Every angle
// asserted below was computed outside this program from the textbook formula
//
//     angle_on_axis = 2 * atan(sensor_mm / (2 * focal_mm))
//     tan(h/2) = tan(v/2) * aspect
//
// and pasted here as a constant. The one external cross-check the spec offered
// — "12 mm on Super 35 is about 92 degrees horizontal, 105 diagonal"
// (CAMERA_LENS_SPEC §2) — is asserted explicitly, so the table is anchored to a
// number nobody in this lane produced.
//
// WHAT ELSE IT GATES
//   * the DEFAULTS DO NOT MOVE ANYTHING: a camera made today has the same
//     angle, the same focal length and the same projection matrix as one made
//     before this phase existed (vertical fit, squeeze 1, no shift);
//   * sensorWidth is no longer inert — a horizontal fit binds through it, and
//     the anamorphic squeeze multiplies it;
//   * authorMode still decides which view of the angle survives a filmback
//     change, now for the fit and the squeeze as well as the sensor pair;
//   * the LENS SHIFT conversions are exact inverses, including through Ogre's
//     near/focal scaling (the trap in §1), and the document's own projection
//     matrix is the same off-axis frustum the engine builds;
//   * the DEPTH OF FIELD numbers, against hand-computed optics, including the
//     hyperfocal identity DoF(H) = [H/2, infinity).

#include <QCoreApplication>
#include <QVariant>

#include <cmath>
#include <cstdio>

#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/cameranode.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg) do { ++checks; if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

#define CHECK_NEAR(actual, expected, tol, msg) do { ++checks; \
    const double _a = (actual), _e = (expected); \
    if (near(_a, _e, tol)) std::printf("ok:   %s (%.4f)\n", msg, _a); \
    else { std::printf("FAIL: %s — got %.6f, expected %.6f\n", msg, _a, _e); ++failures; } \
} while (0)

using iris::CameraSensorFit;
namespace lens = iris::lens;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- 1. THE GOLDEN FOV TABLE -----------------------------------------
    //
    // Full frame, 36 x 24, VERTICAL fit (the historical binding), at a 3:2
    // frame — the sensor's own shape, so the horizontal column is the sensor's
    // width column too. Hand-computed; see the file header.
    {
        struct Row { float focal, vertical, horizontal, diagonal; };
        static const Row table[] = {
            {  12.0f, 90.0000f, 112.6199f, 121.9657f },
            {  16.0f, 73.7398f,  96.7329f, 107.0267f },
            {  24.0f, 53.1301f,  73.7398f,  84.0622f },
            {  35.0f, 37.8493f,  54.4322f,  63.4400f },
            {  50.0f, 26.9915f,  39.5978f,  46.7930f },
            {  85.0f, 16.0714f,  23.9132f,  28.5583f },
            { 100.0f, 13.6855f,  20.4079f,  24.4137f },
            { 105.0f, 13.0396f,  19.4552f,  23.2837f },
            { 200.0f,  6.8673f,  10.2855f,  12.3470f },
        };
        lens::Filmback fb;
        fb.sensorWidth = 36.0f; fb.sensorHeight = 24.0f;
        fb.fit = CameraSensorFit::Vertical;
        fb.aspect = 36.0f / 24.0f;
        for (const Row &r : table) {
            char msg[160];
            const float v = lens::verticalFovDegFromFocal(fb, r.focal);
            std::snprintf(msg, sizeof(msg), "%.0fmm on full frame is %.4f deg vertical", r.focal,
                          double(r.vertical));
            CHECK_NEAR(v, r.vertical, 0.01, msg);
            std::snprintf(msg, sizeof(msg), "…and %.4f deg horizontal", double(r.horizontal));
            CHECK_NEAR(lens::horizontalFovDeg(v, fb.aspect), r.horizontal, 0.01, msg);
            std::snprintf(msg, sizeof(msg), "…and %.4f deg diagonal", double(r.diagonal));
            CHECK_NEAR(lens::diagonalFovDeg(v, fb.aspect), r.diagonal, 0.01, msg);
            // The inverse, on the same row, to the same tolerance.
            std::snprintf(msg, sizeof(msg), "…and %.4f deg comes back as %.0fmm",
                          double(r.vertical), r.focal);
            CHECK_NEAR(lens::focalFromVerticalFovDeg(fb, r.vertical), r.focal, 0.005, msg);
        }
    }

    // Super 35, 24.89 x 18.66, HORIZONTAL fit at the sensor's own aspect — the
    // cine convention, and the column the spec's external claim lives in.
    {
        struct Row { float focal, horizontal, vertical, diagonal; };
        static const Row table[] = {
            {  12.0f, 92.0858f, 75.7301f, 104.6992f },
            {  24.0f, 54.8172f, 42.4872f,  65.8932f },
            {  35.0f, 39.1479f, 29.8526f,  47.9207f },
            {  50.0f, 27.9538f, 21.1397f,  34.5597f },
            {  85.0f, 16.6592f, 12.5280f,  20.7395f },
            { 105.0f, 13.5188f, 10.1556f,  16.8523f },
            { 200.0f,  7.1213f,  5.3418f,   8.8939f },
        };
        lens::Filmback fb;
        fb.sensorWidth = 24.89f; fb.sensorHeight = 18.66f;
        fb.fit = CameraSensorFit::Horizontal;
        fb.aspect = 24.89f / 18.66f;
        for (const Row &r : table) {
            char msg[160];
            const float v = lens::verticalFovDegFromFocal(fb, r.focal);
            std::snprintf(msg, sizeof(msg), "%.0fmm on Super 35 is %.4f deg vertical", r.focal,
                          double(r.vertical));
            CHECK_NEAR(v, r.vertical, 0.01, msg);
            std::snprintf(msg, sizeof(msg), "…and %.4f deg horizontal", double(r.horizontal));
            CHECK_NEAR(lens::horizontalFovDeg(v, fb.aspect), r.horizontal, 0.01, msg);
            std::snprintf(msg, sizeof(msg), "…and %.4f deg diagonal", double(r.diagonal));
            CHECK_NEAR(lens::diagonalFovDeg(v, fb.aspect), r.diagonal, 0.01, msg);
            std::snprintf(msg, sizeof(msg), "…and inverts back to %.0fmm", r.focal);
            CHECK_NEAR(lens::focalFromVerticalFovDeg(fb, v), r.focal, 0.005, msg);
        }
        // THE EXTERNAL ANCHOR (CAMERA_LENS_SPEC §2): "12mm on Super35 is about
        // 92 degrees horizontal / 105 diagonal — wide RECTILINEAR".
        const float v12 = lens::verticalFovDegFromFocal(fb, 12.0f);
        CHECK(near(lens::horizontalFovDeg(v12, fb.aspect), 92.0, 0.5) &&
              near(lens::diagonalFovDeg(v12, fb.aspect), 105.0, 0.5),
              "the spec's external anchor holds: 12mm on Super 35 = ~92 deg across, ~105 corner");
    }

    // ---- 2. THE DEFAULTS DO NOT MOVE -------------------------------------
    {
        auto cam = iris::CameraNode::create();
        CHECK(cam->sensorFit == CameraSensorFit::Vertical && cam->anamorphicSqueeze == 1.0f &&
              cam->lensShiftX == 0.0f && cam->lensShiftY == 0.0f,
              "a new camera is vertical-fit, spherical and unshifted");
        CHECK_NEAR(cam->angle, 45.0, 1e-4, "the default angle is still 45 degrees");
        // 24 / (2 * tan(22.5 deg)) = 28.970563 mm — the number CAMERAS_SPEC
        // recorded when the sensor pair landed.
        CHECK_NEAR(cam->focalLength(), 28.970563, 1e-4,
                   "the default lens is still the same 28.97mm");
        // The projection matrix must be BIT-identical to the symmetric one: a
        // pixel suite that never heard of lens shift may not move.
        cam->aspectRatio = 16.0f / 9.0f;
        cam->updateCameraMatrices();
        iris::Mat4 expected;
        expected.setToIdentity();
        expected.perspective(cam->angle, cam->aspectRatio, cam->nearClip, cam->farClip);
        bool identical = true;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                if (cam->projMatrix(r, c) != expected(r, c)) identical = false;
        CHECK(identical, "an unshifted camera's projection matrix is BIT-identical to the "
                         "symmetric perspective it always was");
    }

    // ---- 3. sensorWidth IS NO LONGER INERT --------------------------------
    {
        auto cam = iris::CameraNode::create();
        cam->aspectRatio = 16.0f / 9.0f;
        cam->setSensorSize(36.0f, 24.0f);
        cam->setFocalLength(50.0f);                       // vertical fit
        const float verticalFitAngle = cam->angle;
        cam->sensorWidth = 50.0f;                          // a raw width change…
        CHECK_NEAR(cam->angle, verticalFitAngle, 1e-5,
                   "on a VERTICAL fit the sensor width still changes nothing");

        cam->sensorWidth = 36.0f;
        cam->setSensorFit(CameraSensorFit::Horizontal);
        // 50mm, 36mm wide, 16:9: h = 2*atan(36/100) = 39.5978 deg,
        // v = 2*atan(tan(h/2) / (16/9)) = 22.8952 deg.
        CHECK_NEAR(cam->angle, 22.8952, 0.001,
                   "a horizontal fit binds the lens through the sensor WIDTH and the aspect");
        CHECK_NEAR(cam->focalLength(), 50.0, 0.001,
                   "…and the focal length is unchanged: authorMode was Millimeters");
        CHECK_NEAR(cam->horizontalFov(), 39.5978, 0.001,
                   "…the horizontal angle is the one the sensor width asked for");
    }

    // ---- 4. THE ANAMORPHIC SQUEEZE ---------------------------------------
    {
        auto cam = iris::CameraNode::create();
        cam->aspectRatio = 2.39f;
        cam->setSensorSize(24.89f, 18.66f);
        cam->setSensorFit(CameraSensorFit::Horizontal);
        cam->setFocalLength(50.0f);
        const float spherical = cam->horizontalFov();
        cam->setAnamorphicSqueeze(2.0f);
        const float squeezed = cam->horizontalFov();
        // A 2x anamorphic 50 sees what a spherical 25 sees: 2*atan(24.89/50).
        CHECK_NEAR(squeezed, 2.0 * std::atan(24.89 / 50.0) * 180.0 / 3.14159265358979,
                   0.001, "a 2x squeeze makes a 50mm see as wide as a spherical 25mm");
        CHECK(squeezed > spherical + 10.0f, "…which is a great deal wider than spherical");
        CHECK_NEAR(cam->focalLength(), 50.0, 0.001,
                   "…and it is still a 50mm lens (authorMode kept the millimetres)");

        // On a VERTICAL fit the squeeze is inert — the honest answer, not a fudge.
        auto vert = iris::CameraNode::create();
        vert->setFocalLength(50.0f);
        const float before = vert->angle;
        vert->setAnamorphicSqueeze(2.0f);
        CHECK_NEAR(vert->angle, before, 1e-5,
                   "the squeeze does nothing on a vertical fit, and says so by doing nothing");
    }

    // ---- 5. AUTO FIT ------------------------------------------------------
    {
        CHECK(lens::fitAxis(CameraSensorFit::Auto, 16.0f / 9.0f) == lens::FitAxis::Horizontal &&
              lens::fitAxis(CameraSensorFit::Auto, 9.0f / 16.0f) == lens::FitAxis::Vertical &&
              lens::fitAxis(CameraSensorFit::Auto, 1.0f) == lens::FitAxis::Horizontal,
              "auto binds through the LARGER image axis (square counts as landscape)");
        // Blender's rule to the letter: auto uses the sensor WIDTH on both
        // branches, so a portrait frame's vertical angle comes from the width.
        lens::Filmback fb;
        fb.sensorWidth = 36.0f; fb.sensorHeight = 24.0f;
        fb.fit = CameraSensorFit::Auto; fb.aspect = 9.0f / 16.0f;
        CHECK_NEAR(lens::verticalFovDegFromFocal(fb, 50.0f),
                   2.0 * std::atan(36.0 / 100.0) * 180.0 / 3.14159265358979, 0.001,
                   "auto on a PORTRAIT frame binds the sensor width through the vertical axis");
    }

    // ---- 6. authorMode still decides who survives -------------------------
    {
        auto degrees = iris::CameraNode::create();
        degrees->aspectRatio = 1.5f;
        degrees->setFieldOfViewDegrees(45.0f);      // authored in degrees
        degrees->setSensorFit(CameraSensorFit::Horizontal);
        CHECK_NEAR(degrees->angle, 45.0, 1e-5,
                   "a camera authored in DEGREES keeps its framing across a fit change");

        auto mm = iris::CameraNode::create();
        mm->aspectRatio = 1.5f;
        mm->setFocalLength(35.0f);                  // authored in millimetres
        mm->setSensorFit(CameraSensorFit::Horizontal);
        CHECK_NEAR(mm->focalLength(), 35.0, 0.001,
                   "a camera authored in MILLIMETRES keeps its lens across a fit change");
        CHECK(std::fabs(mm->angle - 45.0f) > 1.0f, "…and its framing moved, as it must");

        // The aspect is part of the binding when the fit is horizontal.
        const float wasAngle = mm->angle;
        mm->setAspectRatio(2.39f);
        CHECK_NEAR(mm->focalLength(), 35.0, 0.001,
                   "changing the frame's shape keeps the LENS on a horizontal fit");
        CHECK(mm->angle < wasAngle, "…and narrows the vertical angle, as a wider frame must");

        auto vertical = iris::CameraNode::create();
        vertical->setFocalLength(35.0f);
        const float keep = vertical->angle;
        vertical->setAspectRatio(2.39f);
        CHECK_NEAR(vertical->angle, keep, 1e-5,
                   "…while a VERTICAL fit is untouched by the aspect, exactly as before");
    }

    // ---- 7. LENS SHIFT: the conversions are exact inverses ---------------
    {
        const float half = lens::halfExtentAtNear(45.0f, 0.1f);
        CHECK_NEAR(half, std::tan(22.5 * 3.14159265358979 / 180.0) * 0.1, 1e-7,
                   "the near-plane half extent is tan(angle/2) * near");
        for (float shift : { -1.0f, -0.37f, 0.0f, 0.25f, 0.5f, 1.0f }) {
            const float offset = lens::nearOffsetFromShift(shift, half);
            CHECK_NEAR(lens::shiftFromNearOffset(offset, half), shift, 1e-6,
                       "fraction -> near offset -> fraction round-trips");
        }
        // A full frame of shift moves the frustum by the WHOLE frame width.
        CHECK_NEAR(lens::nearOffsetFromShift(1.0f, half), 2.0 * half, 1e-7,
                   "a shift of 1.0 is one whole frame at the near plane");

        // THE TRAP (§1): Ogre scales the offset by near / stereoFocalLength, and
        // that stereo focal length is NOT a lens. Round-trip through it at a
        // non-default value so the cancellation-at-1.0 coincidence cannot hide
        // an error.
        for (float stereoFocal : { 1.0f, 2.5f }) {
            for (float near_ : { 0.05f, 0.1f, 3.0f }) {
                const float h = lens::halfExtentAtNear(60.0f, near_);
                const float fo = lens::ogreFrustumOffset(0.3f, h, near_, stereoFocal);
                CHECK_NEAR(lens::shiftFromOgreFrustumOffset(fo, h, near_, stereoFocal), 0.3,
                           1e-5, "fraction -> Ogre frustum offset -> fraction round-trips");
                // …and the value Ogre would compute from it IS the near offset
                // we asked for: nearOffset = frustumOffset * (near / focal).
                CHECK_NEAR(fo * (near_ / stereoFocal), lens::nearOffsetFromShift(0.3f, h),
                           1e-6, "Ogre's own near/focal scaling lands on the asked-for offset");
            }
        }
    }

    // ---- 8. the document's projection matrix follows the shift -----------
    // Picking rays come out of this matrix, so it must be the same off-axis
    // frustum the engine builds — otherwise a click lands where the shot is not.
    {
        auto cam = iris::CameraNode::create();
        cam->aspectRatio = 1.0f;
        cam->angle = 60.0f;
        cam->lensShiftX = 0.25f;
        cam->updateCameraMatrices();
        // A point straight down the camera's axis at 10 units. Shifting the
        // frustum a quarter frame to the right moves that point half a unit of
        // NDC to the LEFT (a frame spans 2 NDC units).
        const iris::Vec4 clip = cam->projMatrix * iris::Vec4(0.0f, 0.0f, -10.0f, 1.0f);
        CHECK_NEAR(clip.x() / clip.w(), -0.5, 1e-4,
                   "a quarter-frame shift moves the axis point half an NDC unit");
        cam->lensShiftX = 0.0f;
        cam->lensShiftY = 0.5f;
        cam->updateCameraMatrices();
        const iris::Vec4 clipY = cam->projMatrix * iris::Vec4(0.0f, 0.0f, -10.0f, 1.0f);
        CHECK_NEAR(clipY.y() / clipY.w(), -1.0, 1e-4,
                   "…and a half-frame vertical shift moves it a whole NDC unit");
    }

    // ---- 9. DEPTH OF FIELD, against hand-computed optics ------------------
    {
        // Full frame: c = sqrt(36^2 + 24^2) / 1500 = 0.0288444 mm.
        CHECK_NEAR(lens::circleOfConfusionMm(36.0f, 24.0f), 0.0288444, 1e-6,
                   "full frame's circle of confusion is the published 0.0288mm");
        CHECK_NEAR(lens::circleOfConfusionMm(24.89f, 18.66f), 0.0207387, 1e-6,
                   "…and Super 35's is 0.0207mm");

        struct Row { float f, N, s, cw, ch, H, near_, far_; };
        static const Row table[] = {
            //  f     N     s     sensor        H          near      far
            {  50.0f, 2.8f,  3.0f, 36.0f, 24.0f, 31.004252f, 2.738971f,  3.316023f },
            {  50.0f, 1.4f,  2.0f, 36.0f, 24.0f, 61.958504f, 1.938927f,  2.065045f },
            {  35.0f, 2.0f,  4.0f, 24.89f, 18.66f, 29.569203f, 3.526556f, 4.620277f },
            {  85.0f, 4.0f, 10.0f, 36.0f, 24.0f, 62.705452f, 8.633082f, 11.881210f },
        };
        for (const Row &r : table) {
            const lens::FocusInfo i =
                lens::focusInfo(r.f, r.N, r.s, lens::circleOfConfusionMm(r.cw, r.ch));
            char msg[160];
            std::snprintf(msg, sizeof(msg), "%.0fmm f/%.1f at %.0fm: hyperfocal %.4fm",
                          r.f, r.N, r.s, r.H);
            CHECK_NEAR(i.hyperfocal, r.H, 1e-4, msg);
            std::snprintf(msg, sizeof(msg), "…near limit %.4fm", r.near_);
            CHECK_NEAR(i.nearLimit, r.near_, 1e-4, msg);
            std::snprintf(msg, sizeof(msg), "…far limit %.4fm", r.far_);
            CHECK_NEAR(i.farLimit, r.far_, 1e-4, msg);
        }

        // THE HYPERFOCAL IDENTITY: focused AT H, the sharp range is exactly
        // [H/2, infinity). This is the check that the two formulas belong to
        // each other and not merely to the same textbook page.
        const float c = lens::circleOfConfusionMm(36.0f, 24.0f);
        const lens::FocusInfo h = lens::focusInfo(50.0f, 2.8f, 31.004252f, c);
        // "Infinity OR astronomically far": the hyperfocal distance is a
        // SINGULARITY of the far-limit formula, so whether a float focus
        // distance lands a hair under or over it decides between 1e9 metres and
        // a literal infinity. Both mean the same thing to a photographer and
        // pinning either one would be pinning float rounding.
        CHECK(std::isinf(h.farLimit) || h.farLimit > 1.0e5,
              "focusing at the hyperfocal distance reaches (effectively) infinity");
        CHECK_NEAR(h.nearLimit, h.hyperfocal / 2.0, 1e-3,
                   "…and its near limit is exactly half the hyperfocal distance");
        // Wide open at a short lens: everything past a metre and a bit is sharp.
        const lens::FocusInfo deep = lens::focusInfo(24.0f, 8.0f, 5.0f, c);
        CHECK(std::isinf(deep.farLimit),
              "a 24mm at f/8 focused at 5m is sharp to infinity (5m is past its 2.52m hyperfocal)");
        // Degenerate input is answered, not divided by.
        const lens::FocusInfo none = lens::focusInfo(50.0f, 0.0f, 3.0f, c);
        CHECK(none.hyperfocal == 0.0 && none.farLimit == 0.0,
              "a zero f-stop reports zeroes instead of dividing by them");
    }

    // ---- 10. the node's own focusInfo, and the focus fields --------------
    {
        auto cam = iris::CameraNode::create();
        cam->setSensorSize(36.0f, 24.0f);
        cam->setFocalLength(50.0f);
        cam->fStop = 2.8f;
        cam->focusDistance = 3.0f;
        const lens::FocusInfo i = cam->focusInfo();
        CHECK_NEAR(i.hyperfocal, 31.004252, 1e-3,
                   "CameraNode::focusInfo uses its own lens, aperture and sensor");
        CHECK_NEAR(i.focusDistance, 3.0, 1e-6, "…and echoes the focus distance it used");

        CHECK(cam->setPropertyValue("bladeCount", QVariant(99)) && cam->bladeCount == 16,
              "bladeCount is clamped to a real diaphragm");
        CHECK(cam->setPropertyValue("lensShiftX", QVariant(5.0f)) && cam->lensShiftX == 1.0f,
              "lens shift is clamped to a frame");
        CHECK(cam->setPropertyValue("minFocusDistance", QVariant(-2.0f)) && cam->minFocusDistance == 0.0f,
              "a negative minimum focus distance is refused");
    }

    // ---- 11. focus smoothing is a pure function of dt ---------------------
    {
        CHECK_NEAR(lens::smoothTowards(0.0f, 10.0f, 1.0f, 0.0f), 10.0,
                   1e-6, "a zero dt snaps (there is no time to ease over)");
        CHECK_NEAR(lens::smoothTowards(0.0f, 10.0f, 0.0f, 1.0f), 10.0,
                   1e-6, "a zero speed snaps");
        // One e-fold: 63.2% of the way there.
        CHECK_NEAR(lens::smoothTowards(0.0f, 10.0f, 1.0f, 1.0f), 10.0 * (1.0 - std::exp(-1.0)),
                   1e-5, "one e-fold covers 63.2% of the distance");
        // Framerate independence: two half-steps equal one whole step.
        const float once = lens::smoothTowards(0.0f, 10.0f, 4.0f, 0.5f);
        const float twice = lens::smoothTowards(lens::smoothTowards(0.0f, 10.0f, 4.0f, 0.25f),
                                                10.0f, 4.0f, 0.25f);
        CHECK_NEAR(twice, once, 1e-5, "smoothing is framerate independent");
    }

    // ---- 12. the preset tables --------------------------------------------
    {
        int fbCount = 0, lensCount = 0;
        const lens::FilmbackPreset *fbs = lens::filmbackPresets(fbCount);
        const lens::LensPreset *ls = lens::lensPresets(lensCount);
        CHECK(fbCount >= 8 && lensCount >= 7, "both preset tables are populated");
        bool sane = true, fullFrame = false, anamorphic = false;
        for (int i = 0; i < fbCount; ++i) {
            if (!(fbs[i].sensorWidth > 0.0f && fbs[i].sensorHeight > 0.0f &&
                  fbs[i].squeeze > 0.0f && fbs[i].name && *fbs[i].name)) sane = false;
            if (QString::fromLatin1(fbs[i].name).startsWith("Full Frame") &&
                fbs[i].sensorWidth == 36.0f && fbs[i].sensorHeight == 24.0f) fullFrame = true;
            if (fbs[i].squeeze == 2.0f) anamorphic = true;
        }
        CHECK(sane, "every filmback preset has a name and a positive sensor");
        CHECK(fullFrame, "Full Frame is 36 x 24, the value the class defaults to");
        CHECK(anamorphic, "there is a 2x anamorphic filmback to prove the squeeze row works");
        bool lensSane = true;
        for (int i = 0; i < lensCount; ++i)
            if (!(ls[i].focalMm > 0.0f && ls[i].minFStop > 0.0f && ls[i].name && ls[i].note))
                lensSane = false;
        CHECK(lensSane, "every lens preset has a focal length, an f-stop and a note");
        // Both briefed prime sets are present (the spec's and the build brief's).
        const float wanted[] = { 12, 16, 24, 35, 50, 85, 100, 105, 200 };
        bool all = true;
        for (float w : wanted) {
            bool found = false;
            for (int i = 0; i < lensCount; ++i) if (ls[i].focalMm == w) found = true;
            if (!found) all = false;
        }
        CHECK(all, "the prime kit covers 12/16/24/35/50/85/100/105/200mm");
    }

    std::printf(failures == 0 ? "\nALL %d LENS CHECKS PASSED\n" : "\n%d OF %d LENS CHECKS FAILED\n",
                failures == 0 ? checks : failures, checks);
    return failures == 0 ? 0 : 1;
}
