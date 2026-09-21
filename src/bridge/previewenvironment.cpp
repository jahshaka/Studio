#include "bridge/previewenvironment.h"

#include <algorithm>
#include <cmath>
#include <QVariantList>

#include "irisgl/document/assets/livetextures.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/scene.h"
#include "jahshaka/engine/Engine.h"

namespace previewenv {

namespace {

constexpr int   kWidth  = 512;
constexpr int   kHeight = 256;
constexpr float kPi     = 3.14159265358979f;

/// The live-texture identity the preview sky is addressed by. A live reference
/// is a session object (LiveTextures), which is exactly what a generated
/// environment is: never serialised, never pinned, never exported.
const char *kSkyGuid = "jahshaka-preview-studio-environment";

// ---- the picture, in LINEAR radiance ---------------------------------------
//
// A STUDIO IS NOT A SKY, and the difference is the whole design. A sky lights a
// subject from a bright hemisphere and is photographed as a bright backdrop; a
// studio lights it from LAMPS the camera cannot see and photographs it against
// a DARK wall. The environment is therefore split by an axis — up and towards
// the eye (the lamp side, `kLampAxis`), down and away from it (the wall) — with
// three softbox panels on the lamp side and a darker floor under everything.
//
// WHY IT HAS TO BE SPLIT THAT WAY, in numbers (measured, and the reason the
// first attempt at this lane was a bright blank ball): the shipped film curve
// has about eleven stops-of-input between "an 18 % grey card displays at 118"
// and "the write clips", and a MIRROR spends 5.5x of that on the grey card's
// own albedo. So the brightest thing in an environment may be at most ~2.15x
// the mean incident radiance on the subject's face if a chrome ball is not to
// clip. A uniformly bright environment wastes that headroom on light the
// camera looks straight at; a dark far wall costs the subject's FRONT nothing
// (that hemisphere is behind it) and buys the whole budget for the lamps.
//   E_front = PI * Lfront   ->   m = x* / (0.18 * Lfront) = 2.7
//   the brightest panel:  1.0 * 2.7 = 2.7  ->  the curve gives 0.92, i.e. 245.
constexpr float kBackWall  = 0.10f;   ///< the wall the subject is photographed against
constexpr float kLampSide  = 0.65f;   ///< the lit side: the lamps' own room
constexpr float kFloorMul  = 0.35f;   ///< everything below the horizon, times this

/// The lamp side's axis: the preview camera's own direction, tilted UP. Tilting
/// is what shapes the subject — a sphere lit straight down the lens is flat —
/// and the measured diffuse ratio between an up-facing and a down-facing normal
/// is 10.5:1 at this tilt against 5.6:1 with no tilt.
const float kLampAxis[3] = { 0.5145f, 1.2087f, 0.7717f };

/// A smooth 0..1 ramp (the same shape the shaders use).
float smoothstep(float edge0, float edge1, float x)
{
    const float t = edge0 == edge1 ? 0.0f
                                   : std::min(1.0f, std::max(0.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

float srgbEncode(float linear)
{
    const float c = std::min(1.0f, std::max(0.0f, linear));
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float srgbDecode(float encoded)
{
    return encoded <= 0.04045f ? encoded / 12.92f
                               : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

/// THE LAT-LONG CONVENTION, and it is the renderer's, not a second one:
/// SceneMirror's `equirectDir` (u = (atan2(x,-z)+PI)/2PI, v = acos(y)/PI, row 0
/// = the zenith) is what Ogre's SkyEquirectangular_ps.glsl samples with, so a
/// softbox written at a direction here hangs in that direction in the picture.
void directionFor(float u, float v, float &x, float &y, float &z)
{
    const float phi = v * kPi;
    const float s = std::sin(phi);
    y = std::cos(phi);
    const float t = u * 2.0f * kPi - kPi;
    x =  s * std::sin(t);
    z = -s * std::cos(t);
}

/// The room's own radiance in a direction: the lamp side sweeping into the far
/// wall, everything below the horizon dimmed.
float backdrop(float x, float y, float z)
{
    const float n = std::sqrt(kLampAxis[0] * kLampAxis[0] + kLampAxis[1] * kLampAxis[1] +
                              kLampAxis[2] * kLampAxis[2]);
    const float towardsLamps = (x * kLampAxis[0] + y * kLampAxis[1] + z * kLampAxis[2]) / n;
    float r = kBackWall + (kLampSide - kBackWall) * smoothstep(-0.40f, 0.70f, towardsLamps);
    r *= kFloorMul + (1.0f - kFloorMul) * smoothstep(-0.55f, 0.05f, y);
    return r;
}

const std::vector<Softbox> &boxes()
{
    // THE THREE LAMPS. The preview camera stands at (2, 1.2, 3) looking at the
    // origin, and a mirror shows what is BEHIND the camera — so a softbox that
    // is to appear on the sphere hangs above and behind the eye. Key high-left,
    // fill high-right (smaller and a touch dimmer: a fill), and a long overhead
    // panel just past the top of the subject for the rim.
    static const std::vector<Softbox> kBoxes = {
        { "key",      { -0.169f, 0.784f,  0.600f }, 22.0f, 14.0f, 1.00f },
        { "fill",     {  0.805f, 0.590f,  0.045f }, 16.0f, 11.0f, 0.88f },
        { "overhead", {  0.000f, 0.951f, -0.309f }, 20.0f,  9.0f, 0.94f },
    };
    return kBoxes;
}

/// A softbox's radiance in direction `d`: its own level inside the rectangle,
/// feathered to nothing over `kFeatherDeg` outside it. The caller takes the
/// MAXIMUM of the panels and the room, never the sum: a panel is a lamp seen
/// directly, and two of them summed would only clip against the 8-bit ceiling
/// while spending the exposure headroom the note above measures.
float softboxAt(const Softbox &b, float dx, float dy, float dz)
{
    constexpr float kFeatherDeg = 5.0f;
    const float cx = b.dir[0], cy = b.dir[1], cz = b.dir[2];
    const float cosCentre = dx * cx + dy * cy + dz * cz;
    if (cosCentre <= 0.0f) return 0.0f;          // the other half of the sphere
    // The box's own frame: "up" is world up projected off its axis, "right" is
    // the pair's cross product. A lamp hung straight overhead has no preferred
    // up — the degenerate case falls back to world +X.
    float ux = -cx * cy, uy = 1.0f - cy * cy, uz = -cz * cy;
    float ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (ul < 1e-4f) { ux = 1.0f; uy = 0.0f; uz = 0.0f; ul = 1.0f; }
    ux /= ul; uy /= ul; uz /= ul;
    const float rx = uy * cz - uz * cy;
    const float ry = uz * cx - ux * cz;
    const float rz = ux * cy - uy * cx;
    const float a = std::asin(std::min(1.0f, std::max(-1.0f, dx * rx + dy * ry + dz * rz))) * 180.0f / kPi;
    const float e = std::asin(std::min(1.0f, std::max(-1.0f, dx * ux + dy * uy + dz * uz))) * 180.0f / kPi;
    const float fa = 1.0f - smoothstep(b.halfWidthDeg, b.halfWidthDeg + kFeatherDeg, std::fabs(a));
    const float fe = 1.0f - smoothstep(b.halfHeightDeg, b.halfHeightDeg + kFeatherDeg, std::fabs(e));
    return b.radiance * fa * fe;
}

/// The generated picture and, from its QUANTISED texels, the ambient integral —
/// built together because the second is an integral of the first as the GPU
/// samples it (8-bit sRGB, decoded), not of the float values it came from.
struct Built
{
    QImage image;
    float  sh[27] = { 0.0f };
};

/// Cosine-convolved irradiance in 9 SH bands, in the basis and the units
/// Scene::setAmbientSh documents. THE SAME ACCUMULATOR the engine integrates a
/// captured sky with (OgreSky.cpp's ShAccum, constants included) — the value
/// this hands the engine has to be the value the engine would have measured.
struct ShAccum
{
    double a[9][3] = {};
    void add(float x, float y, float z, double r, double g, double b, double w)
    {
        const double bi[9] = { 1.0, y, z, x, double(x) * y, double(y) * z,
                               3.0 * double(z) * z - 1.0, double(z) * x,
                               double(x) * x - double(y) * y };
        for (int i = 0; i < 9; ++i) {
            const double f = bi[i] * w;
            a[i][0] += r * f; a[i][1] += g * f; a[i][2] += b * f;
        }
    }
    void finish(float out[27]) const
    {
        static const double k[9] = {
            0.0795774715,
            0.1591549431, 0.1591549431, 0.1591549431,
            0.2984155183, 0.2984155183,
            0.0248679599,
            0.2984155183,
            0.0746038796
        };
        for (int i = 0; i < 9; ++i)
            for (int c = 0; c < 3; ++c) out[i * 3 + c] = float(a[i][c] * k[i]);
    }
};

const Built &built()
{
    static const Built b = [] {
        Built out;
        out.image = QImage(kWidth, kHeight, QImage::Format_RGBA8888);
        ShAccum accum;
        const double dPhi = 2.0 * double(kPi) / double(kWidth);
        const double dTheta = double(kPi) / double(kHeight);
        for (int j = 0; j < kHeight; ++j) {
            const float v = (float(j) + 0.5f) / float(kHeight);
            uchar *row = out.image.scanLine(j);
            const double w = std::sin(double(v) * double(kPi)) * dTheta * dPhi;
            for (int i = 0; i < kWidth; ++i) {
                const float u = (float(i) + 0.5f) / float(kWidth);
                float x, y, z;
                directionFor(u, v, x, y, z);
                float radiance = backdrop(x, y, z);
                for (const Softbox &panel : boxes())
                    radiance = std::max(radiance, softboxAt(panel, x, y, z));
                // NEUTRAL, deliberately: a studio that tints is a studio that
                // lies about a material's colour.
                const uchar code = uchar(std::lround(srgbEncode(radiance) * 255.0f));
                row[i * 4 + 0] = code;
                row[i * 4 + 1] = code;
                row[i * 4 + 2] = code;
                row[i * 4 + 3] = 255;
                const double decoded = double(srgbDecode(float(code) / 255.0f));
                accum.add(x, y, z, decoded, decoded, decoded, w);
            }
        }
        accum.finish(out.sh);
        return out;
    }();
    return b;
}

}   // namespace

const std::vector<Softbox> &softboxes() { return boxes(); }

const QImage &image() { return built().image; }

namespace { const float kViewNormal[3] = { 0.5145f, 0.3087f, 0.7717f }; }

void viewDirection(float out[3])
{
    out[0] = kViewNormal[0]; out[1] = kViewNormal[1]; out[2] = kViewNormal[2];
}

QSize size() { return QSize(kWidth, kHeight); }

const float *ambientSh() { return built().sh; }

/// The environment's mean incident radiance for a surface whose normal is `n`,
/// evaluated from the nine bands in the basis Scene::setAmbientSh documents.
/// This is what the shader computes per pixel; evaluating it here is how the
/// exposure below can be derived instead of tuned.
float meanIncidentRadiance(const float n[3])
{
    const float *sh = ambientSh();
    const float x = n[0], y = n[1], z = n[2];
    const float basis[9] = { 1.0f, y, z, x, x * y, y * z, 3.0f * z * z - 1.0f, z * x,
                             x * x - y * y };
    float sum = 0.0f;
    for (int i = 0; i < 9; ++i) sum += sh[i * 3] * basis[i];   // neutral: one channel is all of them
    return std::max(0.0f, sum);
}

float exposureEv()
{
    // DERIVED, through the app's one meter (iris::lens, EXPOSURE-1) — the same
    // function the default world's grade comes from.
    //
    // THE KEY IRRADIANCE IS THE ONE ON THE SUBJECT'S FACE, not the environment's
    // sphere average: a grey card is held facing the camera, and in a studio the
    // lamps are on the camera's side and the wall behind the subject is dark, so
    // the two differ by a factor of two here. Using the average put the picture
    // 0.35 stops hot (measured: an 18 % grey ball at 133/255 instead of 118).
    // `kLampAxis` normalised is not the camera's direction; the camera's is, and
    // it is the environment's own authored viewpoint.
    static const float stops = [] {
        const float key = kPi * meanIncidentRadiance(kViewNormal);
        return iris::lens::exposureChainToStops(iris::lens::exposureForKeyIrradiance(key));
    }();
    return stops;
}

float exposureChain() { return iris::lens::exposureStopsToChain(exposureEv()); }

void apply(const iris::ScenePtr &scene)
{
    if (!scene) return;
    // ONE session texture for every preview surface in the process: the pixels
    // are identical by construction, and one upload per mirror is the whole
    // cost of the environment.
    iris::Texture2DPtr sky = iris::LiveTextures::find(QString::fromLatin1(kSkyGuid));
    if (!sky) {
        sky = iris::LiveTextures::create(QString::fromLatin1(kSkyGuid),
                                         QStringLiteral("Preview studio environment"),
                                         kWidth, kHeight, false);
        if (sky) sky->writeLive(image());
    }
    if (sky) {
        scene->setSkyTexture(sky);
        scene->skyType = iris::SkyType::EQUIRECTANGULAR;
    }
    // A PREVIEW IS A PHOTOGRAPH OF AN ASSET, not of a world: no sun in the sky
    // (SKY_LIGHT_SPEC round-2 item 9), no air, no shadows.
    scene->sunDiscVisible = false;
    scene->fogEnabled = false;
    scene->shadowEnabled = false;
    // THE GRADE. Manual, at the studio's own EV, so every preview surface —
    // the dock, a material tile, an asset tile — develops the same picture.
    scene->exposureMode = iris::ExposureMode::Manual;
    scene->exposure = exposureEv();
}

void pushAmbient(jahshaka::engine::Scene *scene)
{
    if (!scene) return;
    // BOTH HALVES (Engine.h, setEnvironmentLightScale): the nine bands are the
    // environment's diffuse contribution, the gain is its specular one. A host
    // that pushes only the first leaves a mirror reflecting an environment that
    // lights nothing.
    scene->setAmbientSh(ambientSh());
    scene->setEnvironmentLightScale(1.0f);
}

QVariantMap describe()
{
    QVariantMap out;
    out.insert(QStringLiteral("exposureEv"), double(exposureEv()));
    out.insert(QStringLiteral("exposureChain"), double(exposureChain()));
    QVariantMap sz;
    sz.insert(QStringLiteral("width"), kWidth);
    sz.insert(QStringLiteral("height"), kHeight);
    out.insert(QStringLiteral("size"), sz);
    out.insert(QStringLiteral("meanRadiance"), double(ambientSh()[0]));
    out.insert(QStringLiteral("keyRadiance"), double(meanIncidentRadiance(kViewNormal)));
    QVariantList view;
    view << double(kViewNormal[0]) << double(kViewNormal[1]) << double(kViewNormal[2]);
    out.insert(QStringLiteral("viewDirection"), view);
    QVariantList boxList;
    for (const Softbox &b : boxes()) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), QString::fromLatin1(b.name));
        QVariantList dir;
        dir << double(b.dir[0]) << double(b.dir[1]) << double(b.dir[2]);
        m.insert(QStringLiteral("direction"), dir);
        m.insert(QStringLiteral("halfWidthDeg"), double(b.halfWidthDeg));
        m.insert(QStringLiteral("halfHeightDeg"), double(b.halfHeightDeg));
        m.insert(QStringLiteral("radiance"), double(b.radiance));
        boxList << m;
    }
    out.insert(QStringLiteral("softboxes"), boxList);
    return out;
}

}   // namespace previewenv
