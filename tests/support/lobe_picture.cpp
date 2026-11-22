// lobe_picture (MATERIAL-ENERGY-1 stage 0) - THE OWNER'S BEFORE / AFTER: one PNG of spheres, today's shipped lobe beside
// the corrected one (Form B), rendered on the CPU THROUGH engine.lobe_energy's own code path (tests/support/lobe_energy.h:
// the shipped JahBrdf / JahDiffuseAlbedo / JahSpecularAlbedo text compiled, Form B as the row states it, P1's derived rows).
// Not a row: a tool, run by hand -   lobe_picture <out.png>
//
// THE ROWS (orthographic spheres, the view along -z, roughness 0 / 0.25 / 0.5 / 0.75 / 1 in five columns):
//   1 the WHITE FURNACE - a white non-metal (rho 1, F0 0.04) in a uniform white sky: the pixel is the sky times the total
//     albedo, i.e. the environment term BRDF_EnvMap computes for a uniform sky (today: rho_d jahDiffuseAlbedo + Es;
//     corrected: rho_d ( 1 - Es ) + Es). A sphere that conserves energy and absorbs nothing VANISHES.
//   2 a HEADLAMP - the point light at the camera (L = V), a light grey non-metal (rho 0.8), black sky: the pixel's direct
//     term, BRDF_Default's diffuse (today jahDisneyDiffuse; corrected Form B) plus the shipped specular (jahSpecularLobe).
//   3 a SIDE LIGHT 60 degrees from the camera, the same material: the grazing side.
//   4 a METAL (F0 0.9 / 0.6 / 0.3, metalness 1) in the white sky: no diffuse, so nothing changes - the control.
//   5 (ALSO FOUND, not this fix - the lead's ruling P8 (c)) a silver metal under a LARGE NEAR LAMP (a sphere light, R / d
//     0.3): today's glint (the shipped jahSphereLightWiden: the lobe widened and renormalised at the lamp's centre) beside
//     the TRUE highlight - the shipped POINT lobe (jahSpecularLobe at the material's alpha) integrated over the lamp's solid
//     angle per pixel, by MIS (the balance heuristic) of the cone's and the lobe's own samples, both estimating the same
//     integral. Form B does not touch it.
// THE DISPLAY: as the app's viewport shows it - thumbgrade::codeRGB (tests/support/thumbnailgrade.h) at the document's
// default 0 stops: the tonemapper's fixed exposure iris::lens::exposureMultiplier( exposureStopsToChain( 0 ) ), the film
// curve's one C++ transcription iris::lens::filmRGB, the sRGB encode. Under each pair, the ratios in LINEAR light.
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "lobe_energy.h"
#include "thumbnailgrade.h"

using namespace lobeenergy;

namespace {
constexpr int kTile = 128;
constexpr double kSky = 0.5;          // the white sky's radiance (rows 1 and 4)
constexpr double kLamp = kPi * 0.5;   // a point light's irradiance facing it: a white Lambert surface reads 0.5 head-on
const double kRough[5] = { 0.0, 0.25, 0.5, 0.75, 1.0 };

struct Rgb {
    double c[3] = { 0.0, 0.0, 0.0 };
};
double luminance(const Rgb &v) { return 0.2126 * v.c[0] + 0.7152 * v.c[1] + 0.0722 * v.c[2]; }

struct V3 {
    double x, y, z;
};
V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 mul(V3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) { return mul(a, 1.0 / std::sqrt(dot(a, a))); }
V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
jahglsl::vec3 g3(V3 a) { return jahglsl::vec3(float(a.x), float(a.y), float(a.z)); }

enum class Lobe { Today, Corrected };
enum class Scene { Furnace, Headlamp, Side, Metal, BigLamp };

// the shipped point specular (BRDF_Default's, jahSpecularLobe) of one channel, times kS
double specular(const Channel &c, V3 N, V3 L, V3 V, double r)
{
    return c.kS * jahglsl::jahSpecularLobe(g3(N), g3(L), g3(V), float(r), jahglsl::vec3(float(c.F0)), 1.0f).x;
}
// the diffuse lobe relative to kD, today's or Form B's
double diffuse(Lobe lobe, const Channel &c, double nl, double nv, double vh, double r)
{
    return lobe == Lobe::Today ? today(nl, nv, vh, r) : formB(c, nl, nv, r, formBNorm(c, r));
}

// THE BIG NEAR LAMP (row 5): the lamp's centre direction, its angular radius s = asin( R / d ), its radiance
const V3 kLampDir = norm({ -0.35, 0.35, 0.87 });
const double kLampSize = 0.3;
const double kLampS = std::asin(kLampSize);
const double kLampRadiance = 2.0;
const double kLampIrradiance = kPi * kLampRadiance * kLampSize * kLampSize;   // a uniform sphere facing the point

// TODAY's sphere light: the shipped widening at the lamp's centre, BRDF_Sphere's specular (the point lobe at the widened
// alpha, times the normalisation), lit by the lamp's irradiance at its centre's cosine
double bigLampToday(const Channel &c, V3 N, V3 V, double r)
{
    const double mu = dot(N, kLampDir);
    if (!(mu > 0.0)) return 0.0;
    float wide = 0.0f;
    const float alpha = std::max(float(r * r), 0.001f);
    const double nrm = jahglsl::jahSphereLightWiden(alpha, float(kLampSize), 1.0f, float(mu), wide);
    return kLampIrradiance * mu * nrm * specular(c, N, kLampDir, V, std::sqrt(double(wide)));
}
// THE TRUE highlight: L = L_e int_cone f_s( alpha; L, V ) N.L dw, MIS of the cone (uniform in solid angle) and the lobe
// (the half vector by D( h ) N.h, the reference helper's sampler) with the balance heuristic, n x n stratified each
double bigLampTrue(const Channel &c, V3 N, V3 V, double r)
{
    const int n = 12;
    const double cosS = std::cos(kLampS), coneW = 2.0 * kPi * (1.0 - cosS);
    const double a = std::max(r * r, 0.001), a2 = a * a;
    V3 t = std::fabs(kLampDir.x) < 0.9 ? V3{ 1, 0, 0 } : V3{ 0, 1, 0 };
    const V3 lu = norm(cross(t, kLampDir)), lv = cross(kLampDir, lu);
    V3 tn = std::fabs(N.x) < 0.9 ? V3{ 1, 0, 0 } : V3{ 0, 1, 0 };
    const V3 nu = norm(cross(tn, N)), nv = cross(N, nu);
    auto inCone = [&](V3 L) { return dot(L, kLampDir) >= cosS; };
    auto pdfLobe = [&](V3 L) {
        const V3 H = norm(add(L, V));
        const double nh = dot(N, H), vh = dot(V, H);
        if (!(nh > 0.0) || !(vh > 0.0)) return 0.0;
        const double f = nh * nh * (a2 - 1.0) + 1.0;
        return a2 / (kPi * f * f) * nh / (4.0 * vh);
    };
    const int m = n * n;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            const double u1 = (i + 0.5) / n, u2 = (k + 0.5) / n;
            {   // the cone's sample
                const double ct = 1.0 - u1 * (1.0 - cosS), st = std::sqrt(std::max(0.0, 1.0 - ct * ct)), ph = 2.0 * kPi * u2;
                const V3 L = add(add(mul(lu, st * std::cos(ph)), mul(lv, st * std::sin(ph))), mul(kLampDir, ct));
                const double nl = dot(N, L);
                if (nl > 0.0) {
                    const double pc = 1.0 / coneW, pl = pdfLobe(L);
                    sum += specular(c, N, L, V, r) * nl / (pc + pl) / m;   // f / ( m pc ) x pc / ( pc + pl )
                }
            }
            {   // the lobe's sample
                const double c2 = (1.0 - u1) / (1.0 + (a2 - 1.0) * u1), ch = std::sqrt(c2), sh = std::sqrt(std::max(0.0, 1.0 - c2));
                const double ph = 2.0 * kPi * u2;
                const V3 H = add(add(mul(nu, sh * std::cos(ph)), mul(nv, sh * std::sin(ph))), mul(N, ch));
                const V3 L = add(mul(H, 2.0 * dot(V, H)), mul(V, -1.0));
                const double nl = dot(N, L);
                if (nl > 0.0 && inCone(L)) {
                    const double pc = 1.0 / coneW, pl = pdfLobe(L);
                    if (pl > 0.0) sum += specular(c, N, L, V, r) * nl / (pc + pl) / m;
                }
            }
        }
    return kLampRadiance * sum;
}

// one pixel's linear radiance; `inside` false = the background
Rgb shade(Scene scene, Lobe lobe, double r, double x, double y, bool &inside)
{
    Rgb out;
    const double rr = x * x + y * y;
    inside = rr < 1.0;
    const bool skyRow = scene == Scene::Furnace || scene == Scene::Metal;
    if (!inside) {
        for (double &v : out.c) v = skyRow ? kSky : 0.0;
        return out;
    }
    const V3 N = { x, y, std::sqrt(1.0 - rr) }, V = { 0.0, 0.0, 1.0 };
    const double nv = N.z;
    for (int ch = 0; ch < 3; ++ch) {
        const double metalRho[3] = { 0.9, 0.6, 0.3 };
        const Channel c = scene == Scene::Furnace ? Channel{ 1.0, 0.04, 1.0 }
                        : scene == Scene::Metal ? metallic(metalRho[ch], 1.0)
                        : scene == Scene::BigLamp ? metallic(0.9, 1.0)
                                                  : Channel{ 0.8, 0.04, 1.0 };
        double v = 0.0;
        if (skyRow) {
            const double d = lobe == Lobe::Today ? todayAlbedo(nv, r) : formBAlbedo(c, nv, r);
            v = kSky * (c.rhoD * d + es(c, nv, r));
        } else if (scene == Scene::BigLamp) {
            v = lobe == Lobe::Today ? bigLampToday(c, N, V, r) : bigLampTrue(c, N, V, r);
        } else {
            const double ang = scene == Scene::Headlamp ? 0.0 : 60.0 * kPi / 180.0;
            const V3 L = { std::sin(ang), 0.0, std::cos(ang) };
            const double nl = dot(N, L);
            if (nl > 0.0) {
                const V3 H = norm(add(L, V));
                v = kLamp * nl * (c.rhoD / kPi * diffuse(lobe, c, nl, nv, dot(V, H), r) + specular(c, N, L, V, r));
            }
        }
        out.c[ch] = v;
    }
    return out;
}

struct Tile {
    QImage img;
    double sumY = 0.0, centreY = 0.0;
};
Tile render(Scene scene, Lobe lobe, double r)
{
    Tile t;
    t.img = QImage(kTile, kTile, QImage::Format_RGB32);
    const int ss = scene == Scene::BigLamp ? 1 : 2;   // the big lamp's MIS is costly: one sample a pixel
    for (int py = 0; py < kTile; ++py)
        for (int px = 0; px < kTile; ++px) {
            Rgb acc;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx) {
                    const double x = 2.0 * (px + (sx + 0.5) / ss) / kTile - 1.0, y = 1.0 - 2.0 * (py + (sy + 0.5) / ss) / kTile;
                    bool inside = false;
                    const Rgb v = shade(scene, lobe, r, x, y, inside);
                    for (int ch = 0; ch < 3; ++ch) acc.c[ch] += v.c[ch] / (ss * ss);
                    if (inside) t.sumY += luminance(v) / (ss * ss);
                }
            int code[3];
            for (int ch = 0; ch < 3; ++ch)
                code[ch] = std::clamp(int(std::lround(thumbgrade::codeRGB(acc.c, ch, 0.0f))), 0, 255);
            t.img.setPixel(px, py, qRgb(code[0], code[1], code[2]));
        }
    // the middle: the four pixels around the centre (no pixel sits exactly on it)
    for (int py = kTile / 2 - 1; py <= kTile / 2; ++py)
        for (int px = kTile / 2 - 1; px <= kTile / 2; ++px) {
            bool inside = false;
            t.centreY += luminance(shade(scene, lobe, r, 2.0 * (px + 0.5) / kTile - 1.0, 1.0 - 2.0 * (py + 0.5) / kTile, inside)) / 4.0;
        }
    return t;
}
}   // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: lobe_picture <out.png>\n");
        return 2;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const auto derived = exactMomentTable();
    UseTable useDerived(derived.data());   // the corrected lobe's rows (P1); today's lobe reads no hemispherical row

    struct RowDef {
        Scene scene;
        const char *title, *sub, *left, *right;
    };
    const RowDef rows[5] = {
        { Scene::Furnace, "White ball in an evenly lit white room", "a correct ball returns all the light: it disappears",
          "today", "corrected" },
        { Scene::Headlamp, "Light from the camera", "light grey ball, the lamp behind the viewer", "today", "corrected" },
        { Scene::Side, "Light from the side", "the lamp 60 degrees to the right", "today", "corrected" },
        { Scene::Metal, "Metal in the white room", "check: metal must not change", "today", "corrected" },
        { Scene::BigLamp, "Also found (not this fix): silver ball, big lamp close by", "today's shine beside the true one",
          "today", "true" },
    };
    const int labelW = 260, colW = 2 * kTile + 8 + 28, top = 92, rowH = kTile + 74, margin = 16;
    const int W = labelW + 5 * colW + margin, H = top + 5 * rowH + 44;
    QImage page(W, H, QImage::Format_RGB32);
    page.fill(QColor(40, 40, 42));
    QPainter p(&page);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont big("Sans", 15, QFont::Bold), mid("Sans", 11), small("Sans", 10), tiny("Sans", 9);
    p.setPen(QColor(235, 235, 235));
    p.setFont(big);
    p.drawText(margin, 30, "The same materials, today and corrected");
    p.setFont(mid);
    for (int k = 0; k < 5; ++k) {
        const int x0 = labelW + k * colW;
        p.drawText(QRect(x0, 44, 2 * kTile + 8, 20), Qt::AlignCenter,
                   QString("roughness %1").arg(kRough[k], 0, 'g', 2));
    }
    p.drawText(QRect(labelW, 64, 5 * colW - 28, 20), Qt::AlignCenter, QString::fromUtf8("smoother   ←     →   rougher"));

    std::printf("row, roughness, today middle, corrected middle, ratio middle, today whole, corrected whole, ratio whole\n");
    for (int row = 0; row < 5; ++row) {
        const int y0 = top + row * rowH;
        p.setPen(QColor(235, 235, 235));
        p.setFont(mid);
        p.drawText(QRect(margin, y0 + 20, labelW - 2 * margin, 60), Qt::TextWordWrap, rows[row].title);
        p.setPen(QColor(170, 170, 170));
        p.setFont(small);
        p.drawText(QRect(margin, y0 + 72, labelW - 2 * margin, 50), Qt::TextWordWrap, rows[row].sub);
        for (int k = 0; k < 5; ++k) {
            const int x0 = labelW + k * colW;
            const Tile a = render(rows[row].scene, Lobe::Today, kRough[k]);
            const Tile b = render(rows[row].scene, Lobe::Corrected, kRough[k]);
            p.drawImage(x0, y0, a.img);
            p.drawImage(x0 + kTile + 8, y0, b.img);
            p.setPen(QColor(200, 200, 200));
            p.setFont(tiny);
            p.drawText(QRect(x0, y0 + kTile + 2, kTile, 16), Qt::AlignCenter, rows[row].left);
            p.drawText(QRect(x0 + kTile + 8, y0 + kTile + 2, kTile, 16), Qt::AlignCenter, rows[row].right);
            QString line1, line2;
            const double discArea = kPi * (kTile / 2.0) * (kTile / 2.0);
            if (rows[row].scene == Scene::Furnace) {
                // the ball's brightness against the room's, the whole ball, linear
                const double ta = a.sumY / discArea / kSky, tb = b.sumY / discArea / kSky;
                line1 = QString("ball vs room: %1 → %2").arg(ta, 0, 'f', 2).arg(tb, 0, 'f', 3);
                line2 = QString("(1.000 = the right answer)");
            } else {
                const double rm = b.centreY / a.centreY, rw = b.sumY / a.sumY;
                line1 = QString(rows[row].scene == Scene::BigLamp ? "true vs today: ×%1 overall" : "brighter: ×%1 overall")
                            .arg(rw, 0, 'f', 3);
                line2 = rows[row].scene == Scene::BigLamp ? QString() : QString("×%1 in the middle").arg(rm, 0, 'f', 3);
            }
            p.setPen(QColor(235, 235, 235));
            p.setFont(small);
            p.drawText(QRect(x0 - 4, y0 + kTile + 18, 2 * kTile + 16, 18), Qt::AlignCenter, line1);
            p.setPen(QColor(170, 170, 170));
            p.drawText(QRect(x0 - 4, y0 + kTile + 36, 2 * kTile + 16, 18), Qt::AlignCenter, line2);
            std::printf("%d, %.2f, %.6f, %.6f, %.4f, %.6f, %.6f, %.4f\n", row + 1, kRough[k], a.centreY, b.centreY,
                        b.centreY / a.centreY, a.sumY / discArea, b.sumY / discArea, b.sumY / a.sumY);
        }
    }
    p.setPen(QColor(150, 150, 150));
    p.setFont(tiny);
    p.drawText(margin, H - 16, "Shown as the app's viewport shows it (its default exposure and film look). "
                               "The numbers compare the light itself, not the screen's brightness.");
    p.end();
    if (!page.save(QString::fromLocal8Bit(argv[1]))) {
        std::fprintf(stderr, "lobe_picture: cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("wrote %s (%d x %d)\n", argv[1], W, H);
    return 0;
}
