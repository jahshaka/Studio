// THE GIZMO IS THE SAME SIZE ON SCREEN, EVERYWHERE (owner requirement,
// 2026-09-08: "gizmos are uniform screen size in every scene and never scale
// when zooming" — Unreal's and Maya's behaviour).
//
// WHAT WENT WRONG. Gizmo::updateSize sized the gizmo from `camera->angle`, the
// AUTHORED vertical angle of view. That is not the angle a FREE camera is drawn
// at: past its framing aspect the engine narrows the vertical angle to hold the
// 16:9 horizontal extent (src/viewport/freecamerapolicy.h), and the document
// projects and picks through the same narrowed angle
// (CameraNode::effectiveFovDegrees). Sizing against the authored angle while
// the frame is drawn at the effective one makes the gizmo too big by exactly
// their tangent ratio — in the pre-2026-09-09 Grand Showroom (75-degree camera; re-staged to 45° since — the number stays as the synthetic case) that
// was 1.35x on a 2.4:1 window, and 1.25x on 16:9 under the 95-degree cap the
// same report retired. The owner's words: "the gizmo is huge on the 4 silver
// balls".
//
// WHAT THIS SUITE MEASURES, and why it is not a re-derivation of the fix. It
// takes the gizmo's REAL draw items (the transforms the overlay would render,
// with the meshes gizmomeshes.cpp really builds), transforms their vertices to
// world space, and projects them through the DOCUMENT CAMERA'S OWN
// projection * view matrix — the matrix the engine renders with and the picker
// unprojects. The number asserted is the fraction of the VIEWPORT HEIGHT the
// gizmo covers in normalized device coordinates. If updateSize and the
// projection ever disagree again — about the angle, about the distance, about
// the aspect — this number moves.
//
// THE SWEEP: distances 2 / 5 / 20, authored angles 30 / 45 / 75, aspects 16:9
// and 2.4:1, for all three gizmos. 18 configurations per gizmo, one constant.
//
//   * THE PINNED MEASURE is the WHOLE gizmo's projected height — the NDC
//     bounding box of every handle's every vertex, depth included. Across
//     distances it is invariant to the last digit (moving the camera scales
//     the whole configuration about its own position, and projection is
//     invariant under that) — which is exactly "never scales when zooming".
//     Across angles and aspects it moves by well under a percent, and the
//     residue is honest perspective: a handle that reaches TOWARDS the camera
//     is foreshortened differently at 30 degrees than at 75, in this app for
//     the same reason it is in Maya. 2% covers it with room to spare and
//     catches the 35% defect by two orders of magnitude.
//   * THE TRANSLATE gizmo's Y (up) handle is measured as well and pinned ten
//     times tighter, because it lies entirely in the plane through the gizmo
//     origin and therefore has NO foreshortening term at all: it is the
//     analytically exact form of the claim. The rotation gizmo's Y "handle" is
//     a ring in the XZ plane — edge-on to the camera, so its projected height
//     is a sliver of pure perspective — and the scale gizmo's carries a cube
//     with depth; neither is a planar measure, so neither is pinned that way.
//
// Runs on the headless document graph: no display, no GPU, no pixels.

#include <QGuiApplication>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../support/documentgraph.h"

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "viewport/freecamerapolicy.h"
#include "viewport/gizmo.h"
#include "viewport/rotationgizmo.h"
#include "viewport/scalegizmo.h"
#include "viewport/translationgizmo.h"

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok:   %s\n", msg);                               \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

/// The gizmo's on-screen extent, as a fraction of the viewport HEIGHT.
struct Extent {
    float full = 0.0f;      ///< every handle, depth and all
    float upAxis = 0.0f;    ///< the green (Y) handle alone — no foreshortening
    bool  ok = false;
};

/// Every vertex position of `mesh`, in mesh space. The gizmo meshes are built
/// procedurally by gizmomeshes.cpp, so this reads the same buffer the mirror
/// would upload.
bool meshPositions(iris::Mesh *mesh, std::vector<float> &out)
{
    if (!mesh) return false;
    for (const auto &vb : mesh->getVertexBuffers()) {
        if (!vb || !vb->data) continue;
        const QList<iris::VertexAttribute> attribs = vb->vertexLayout.getAttribs();
        if (attribs.isEmpty() || attribs.first().usage != iris::VertexAttribUsage::Position)
            continue;
        const float *f = reinterpret_cast<const float *>(vb->data);
        out.assign(f, f + vb->dataSize / int(sizeof(float)));
        return out.size() >= 3;
    }
    return false;
}

/// Project a world point to normalized device coordinates through the camera's
/// OWN matrices — the ones the engine renders with and ScenePicker unprojects.
bool toNdc(const iris::Mat4 &viewProj, const iris::Vec3 &p, float &ndcY)
{
    const iris::Vec4 clip = viewProj * iris::Vec4(p.x(), p.y(), p.z(), 1.0f);
    if (clip.w() <= 1e-6f) return false;      // behind the eye
    ndcY = clip.y() / clip.w();
    return true;
}

Extent measure(Gizmo &gizmo, const iris::CameraNodePtr &cam)
{
    Extent e;
    const iris::Vec3 eye = cam->getGlobalPosition();
    const iris::Vec3 dir = (iris::Vec3(0, 0, 0) - eye).normalized();
    const auto items = gizmo.drawItems(eye, dir, dir);
    if (items.isEmpty()) return e;

    cam->updateCameraMatrices();
    const iris::Mat4 viewProj = cam->projMatrix * cam->viewMatrix;

    float minAll = 1e9f, maxAll = -1e9f, minUp = 1e9f, maxUp = -1e9f;
    bool any = false, anyUp = false;
    for (const GizmoDrawItem &it : items) {
        std::vector<float> pos;
        if (!meshPositions(it.mesh.data(), pos)) continue;
        // The GREEN handle is the Y axis in all three gizmos (the shared axis
        // palette: 237,66,66 / 122,204,44 / 58,122,240).
        const bool isUp = it.colour.red() == 122 && it.colour.green() == 204;
        for (size_t v = 0; v + 2 < pos.size(); v += 3) {
            const iris::Vec3 world = it.transform * iris::Vec3(pos[v], pos[v + 1], pos[v + 2]);
            float y = 0.0f;
            if (!toNdc(viewProj, world, y)) continue;
            minAll = std::min(minAll, y); maxAll = std::max(maxAll, y); any = true;
            if (isUp) { minUp = std::min(minUp, y); maxUp = std::max(maxUp, y); anyUp = true; }
        }
    }
    if (!any) return e;
    // NDC spans -1..1 over the full viewport height, so half the NDC extent is
    // the fraction of the height.
    e.full = (maxAll - minAll) * 0.5f;
    e.upAxis = anyUp ? (maxUp - minUp) * 0.5f : 0.0f;
    e.ok = any && anyUp;
    return e;
}

float spreadPercent(const std::vector<float> &v)
{
    if (v.empty()) return 0.0f;
    const float lo = *std::min_element(v.begin(), v.end());
    const float hi = *std::max_element(v.begin(), v.end());
    const float mid = (lo + hi) * 0.5f;
    return mid > 0.0f ? 100.0f * (hi - lo) / mid : 1e9f;
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("gizmo-screen-size-ogre.log");
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    auto doc = iris::Scene::create();
    auto node = iris::SceneNode::create();
    doc->getRootNode()->addChild(node);          // at the origin

    auto cam = iris::CameraNode::create();
    cam->nearClip = 0.05f;
    cam->farClip = 2000.0f;
    // A FREE camera: the editor explorer's own policy, which is the whole point
    // — on the 2.4:1 rows below the hold is engaged and the rendered angle is
    // NOT the authored one.
    cam->setFramingAspect(freecam::kFreeCameraFramingAspect);
    doc->getRootNode()->addChild(cam);

    const float kDistances[] = { 2.0f, 5.0f, 20.0f };
    const float kAngles[]    = { 30.0f, 45.0f, 75.0f };
    const float kAspects[]   = { 16.0f / 9.0f, 2.4f };

    struct Entry { const char *name; Gizmo *g; };
    TranslationGizmo translate;
    RotationGizmo rotate;
    ScaleGizmo scale;
    const Entry kGizmos[] = { { "translate", &translate }, { "rotate", &rotate }, { "scale", &scale } };

    for (const Entry &entry : kGizmos) {
        std::printf("\n== %s gizmo ==\n", entry.name);
        entry.g->setSelectedNode(node);

        std::vector<float> allFull, allUp;
        bool measured = true;
        for (float aspect : kAspects) {
            for (float angle : kAngles) {
                std::vector<float> perFov;
                for (float d : kDistances) {
                    cam->setLocalPos(iris::Vec3(0, 0, d));
                    cam->lookAt(iris::Vec3(0, 0, 0));
                    cam->update(0.0f);
                    cam->angle = angle;
                    cam->setAspectRatio(aspect);
                    cam->updateCameraMatrices();
                    entry.g->updateSize(cam);

                    const Extent e = measure(*entry.g, cam);
                    if (!e.ok) { measured = false; continue; }
                    std::printf("   aspect %.3f  authored fov %4.1f (rendered %5.2f)  d=%5.1f  "
                                "scale %7.3f  up-handle %.4f  full %.4f\n",
                                double(aspect), double(angle), double(cam->effectiveFovDegrees()),
                                double(d), double(entry.g->getGizmoScale()),
                                double(e.upAxis), double(e.full));
                    allUp.push_back(e.upAxis);
                    allFull.push_back(e.full);
                    perFov.push_back(e.full);
                }
                // "NEVER SCALES WHEN ZOOMING": at one lens and one window, the
                // WHOLE gizmo (depth included) covers the same fraction of the
                // frame whether it is 2 or 20 units away.
                const float sp = spreadPercent(perFov);
                std::printf("   -> full-gizmo spread across distances 2/5/20: %.3f%%\n", double(sp));
                if (sp >= 2.0f) {
                    std::printf("FAIL: %s gizmo changes size when the camera dollies "
                                "(aspect %.3f, fov %.0f: %.3f%%)\n",
                                entry.name, double(aspect), double(angle), double(sp));
                    ++failures;
                }
            }
        }
        CHECK(measured, "every configuration produced a measurable gizmo");

        // THE UNIFORMITY CLAIM ITSELF: one constant fraction of the viewport
        // height across all 18 combinations of distance, angle of view and
        // window shape. Before the fix the 2.4:1 rows sat 8% (fov 30) to 35%
        // (fov 75) above the 16:9 ones.
        const float sp = spreadPercent(allFull);
        std::printf("   == %s: projected height %.4f..%.4f of the viewport, spread %.3f%% "
                    "over 18 configurations\n", entry.name,
                    double(*std::min_element(allFull.begin(), allFull.end())),
                    double(*std::max_element(allFull.begin(), allFull.end())), double(sp));
        if (sp < 3.0f) std::printf("ok:   %s gizmo is a CONSTANT fraction of the frame (+-3%%)\n", entry.name);
        else { std::printf("FAIL: %s gizmo is not screen-constant (spread %.3f%%)\n", entry.name, double(sp)); ++failures; }

        // The planar form of the same claim, for the one gizmo that has a
        // purely planar handle (see the note at the top).
        if (entry.g == &translate) {
            const float spUp = spreadPercent(allUp);
            std::printf("   == translate up-handle (no foreshortening term): %.4f..%.4f, "
                        "spread %.3f%%\n",
                        double(*std::min_element(allUp.begin(), allUp.end())),
                        double(*std::max_element(allUp.begin(), allUp.end())), double(spUp));
            CHECK(spUp < 0.2f, "the translate gizmo's planar handle is the SAME fraction of the "
                               "frame at every distance, angle of view and window shape (+-0.2%)");
        }

        entry.g->clearSelectedNode();
    }

    // ---- THE WINDOW SHAPE ITSELF CHANGES NOTHING ---------------------------
    //
    // The sharpest form of the claim, and the one this fix is actually
    // responsible for. The residual spread above is perspective — a function of
    // the RENDERED angle of view, which the framing hold deliberately changes
    // on a wide window. So drive two DIFFERENT window shapes to the SAME
    // rendered angle (16:9 at 45 degrees authored; 2.4:1 at the authored angle
    // whose held value is 45) and the gizmo must cover the same fraction of the
    // frame to a tenth of a percent. It is the "uniform in every scene"
    // requirement with the one legitimate variable held fixed.
    {
        const float hold = freecam::kFreeCameraFramingAspect;
        const float target = 45.0f;                       // the rendered angle both must show
        const float wide = 2.4f;
        const float k = float(M_PI) / 180.0f;
        // Invert the hold: authored = 2*atan(tan(target/2) * aspect / hold).
        const float authoredWide =
            2.0f * std::atan(std::tan(target * 0.5f * k) * (wide / hold)) / k;

        for (const Entry &entry : kGizmos) {
            entry.g->setSelectedNode(node);
            cam->setLocalPos(iris::Vec3(0, 0, 7));
            cam->lookAt(iris::Vec3(0, 0, 0));
            cam->update(0.0f);

            cam->angle = target;
            cam->setAspectRatio(hold);
            cam->updateCameraMatrices();
            entry.g->updateSize(cam);
            const Extent narrow = measure(*entry.g, cam);

            cam->angle = authoredWide;
            cam->setAspectRatio(wide);
            cam->updateCameraMatrices();
            entry.g->updateSize(cam);
            const Extent widened = measure(*entry.g, cam);

            const float diff = 100.0f * std::fabs(widened.full - narrow.full) /
                               ((widened.full + narrow.full) * 0.5f);
            std::printf("   %-9s 16:9 @45 -> %.5f   2.40:1 @%.2f (renders %.2f) -> %.5f   "
                        "difference %.4f%%\n", entry.name, double(narrow.full),
                        double(authoredWide), double(cam->effectiveFovDegrees()),
                        double(widened.full), double(diff));
            if (diff < 0.1f)
                std::printf("ok:   %s gizmo: the window SHAPE changes its screen size by nothing\n",
                            entry.name);
            else { std::printf("FAIL: %s gizmo changes size with the window shape (%.4f%%)\n",
                               entry.name, double(diff)); ++failures; }
            entry.g->clearSelectedNode();
        }
        cam->angle = 45.0f;
        cam->setAspectRatio(hold);
    }

    // ---- ORTHOGRAPHIC: the 2016 rule, unchanged ----------------------------
    // An ortho frame's height IS 2 * orthoSize at every depth, so a fixed
    // multiple of orthoSize is already screen-constant and the fov plays no
    // part. Pinned so the perspective work above cannot quietly redefine it.
    {
        translate.setSelectedNode(node);
        cam->setProjection(iris::CameraProjection::Orthogonal);
        cam->orthoSize = 7.0f;
        for (float d : kDistances) {
            cam->setLocalPos(iris::Vec3(0, 0, d));
            cam->lookAt(iris::Vec3(0, 0, 0));
            cam->update(0.0f);
            translate.updateSize(cam);
        }
        const float s = translate.getGizmoScale();
        std::printf("\n   ortho orthoSize 7 -> gizmoScale %.3f (rule: orthoSize * 5)\n", double(s));
        CHECK(std::fabs(s - 35.0f) < 1e-4f, "ortho sizing is orthoSize * 5, unchanged");
        cam->setProjection(iris::CameraProjection::Perspective);
        translate.clearSelectedNode();
    }

    // ---- THE REGRESSION ITSELF ---------------------------------------------
    // The Grand Showroom's own camera on the owner's ultrawide: sizing from the
    // AUTHORED angle (what the code did) against a frame drawn at the EFFECTIVE
    // one is too big by the tangent ratio. Stated as a number so the defect
    // cannot come back silently.
    {
        translate.setSelectedNode(node);
        cam->setLocalPos(iris::Vec3(0, 0, 8));
        cam->lookAt(iris::Vec3(0, 0, 0));
        cam->update(0.0f);
        cam->angle = 75.0f;
        cam->setAspectRatio(2.4f);
        cam->updateCameraMatrices();
        translate.updateSize(cam);
        const float fixed = translate.getGizmoScale();
        const float authoredHalf = qDegreesToRadians(cam->angle * 0.5f);
        const float renderedHalf = qDegreesToRadians(cam->effectiveFovDegrees() * 0.5f);
        const float wrong = kGizmoScreenFraction * 8.0f * std::tan(authoredHalf);
        std::printf("   Showroom 75 deg at 2.40:1: rendered %.2f deg; gizmoScale %.3f, "
                    "authored-angle sizing would give %.3f (%.2fx too big)\n",
                    double(cam->effectiveFovDegrees()), double(fixed), double(wrong),
                    double(wrong / fixed));
        CHECK(std::fabs(fixed - kGizmoScreenFraction * 8.0f * std::tan(renderedHalf)) < 1e-3f,
              "the gizmo sizes from the RENDERED angle of view, not the authored one");
        CHECK(wrong / fixed > 1.3f,
              "and the defect it fixes was worth 1.3x or more in the shipped Showroom");
        translate.clearSelectedNode();
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
