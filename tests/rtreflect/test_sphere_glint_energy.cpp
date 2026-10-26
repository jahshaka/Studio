// rtreflect.sphere_glint_energy — A SPHERE LIGHT'S GLINT CARRIES THE POINT'S ENERGY (PHOTON-7 stage 11, the lead's ruling:
// a lamp's size widens its glint's distribution, never its energy). CPU: the fork's JahBrdf_piece_all.any is READ (its
// k and its table of mu T, the shipped bytes) and its jahSphereLightWiden mirrored here line for line; the energy is
// then INTEGRATED, independently of the table:
//   the image-space energy of a glint = the integral over view directions V of the lobe D Vis (GGX, the height-correlated
//   Smith visibility - jahSpecularLobe's, F = 1) times the light's cosine mu: per unit irradiance;
//   the point light's at alpha; the widened lobe's at alpha' times the shipped normalisation; and THE REFERENCE, the exact
//   sphere - a cone of uniform radiance carrying the point's flux, 4,096 stratified directions over its solid angle.
// THE LAW: the widened lobe's energy equals the exact sphere's within 1 % at cos 0.6 and 0.9, for R / d = 0.01, 0.04 and
// 0.1, at perceptual roughness 0.05 (the polished metal of rtreflect.sphere_glint) and 0.2.
// THE NEGATIVE CONTROL (the red before): Karis's representative point with ( alpha / alpha' )^2, alpha' = alpha + R / 2d
// - the form built first - is OUT of 1 % at cos 0.6 (its mirror plateau carries an extra 1 / cos V).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

struct V3 { double x, y, z; };
static V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 mul(V3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
static double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { const double l = std::sqrt(dot(a, a)); return mul(a, 1.0 / l); }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static const double kPi = 3.14159265358979323846;

// jahSpecularLobe's D Vis (F = 1), N = +z
static double lobe(V3 L, V3 V, double a)
{
    const double NdL = L.z, NdV = V.z;
    if (!(NdL > 0.0) || !(NdV > 0.0)) return 0.0;
    const V3 H = norm(add(L, V));
    const double a2 = a * a;
    const double NxH2 = H.x * H.x + H.y * H.y;
    const double f = a2 + NxH2 * (1.0 - a2);
    const double D = a2 / (kPi * f * f);
    const double lv = NdL * std::sqrt((-NdV * a2 + NdV) * NdV + a2);
    const double ll = NdV * std::sqrt((-NdL * a2 + NdL) * NdL + a2);
    return D * 0.5 / (lv + ll + 1e-30);
}
// THE INTEGRAL OVER V of D Vis for a light at L (cos mu), by the GGX's own CDF in the half vector (D (N.H) dw_H = du dphi
// / 2pi; V = reflect, dw_V = 4 (L.H) dw_H): a quadrature finer than, and independent of, the table's.
static double T(double a, double mu, int nu = 1200, int nphi = 256)
{
    const V3 L{ std::sqrt(std::max(0.0, 1.0 - mu * mu)), 0.0, mu };
    const double a2 = a * a;
    double sum = 0.0;
    for (int i = 0; i < nu; ++i) {
        const double u = (i + 0.5) / nu;
        const double t2 = a2 * u / (1.0 - u);
        const double ct = 1.0 / std::sqrt(1.0 + t2), st = std::sqrt(std::max(0.0, 1.0 - ct * ct));
        for (int j = 0; j < nphi; ++j) {
            const double ph = (j + 0.5) / nphi * 2.0 * kPi;
            const V3 H{ st * std::cos(ph), st * std::sin(ph), ct };
            const double LdH = dot(L, H);
            if (!(LdH > 0.0)) continue;
            const V3 V = add(mul(H, 2.0 * LdH), mul(L, -1.0));
            if (!(V.z > 0.0)) continue;
            const double NdV = V.z, NdL = mu;
            const double lv = NdL * std::sqrt((-NdV * a2 + NdV) * NdV + a2);
            const double ll = NdV * std::sqrt((-NdL * a2 + NdL) * NdL + a2);
            sum += 0.5 / (lv + ll + 1e-30) * 4.0 * LdH / ct;
        }
    }
    return sum / (double(nu) * nphi);
}

// ---- the shipped function, read from the fork's media ----
static double gK = 0.0;
static std::vector<double> gTab;
static bool readShipped(const std::string &path)
{
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    const size_t k = s.find("kJahSphereLobeK = ");
    if (k == std::string::npos) return false;
    gK = std::atof(s.c_str() + k + 18);
    const size_t t = s.find("kJahLobeMuT[1024] = float[1024](");
    if (t == std::string::npos) return false;
    size_t p = s.find('(', t + 20) + 1;
    const size_t e = s.find(");", p);
    std::string body = s.substr(p, e - p);
    std::stringstream bs(body);
    std::string tok;
    while (std::getline(bs, tok, ',')) gTab.push_back(std::atof(tok.c_str()));
    return gTab.size() == 1024u && gK > 0.0;
}
static double lobeMuT(double alpha, double mu)
{
    const double q0 = 0.0316227766, p0 = 0.2236067977;
    const double fq = std::min(std::max((std::sqrt(std::max(alpha, 0.001)) - q0) / (1.0 - q0), 0.0), 1.0) * 31.0;
    const double fm = std::min(std::max((std::sqrt(std::min(std::max(mu, 0.05), 1.0)) - p0) / (1.0 - p0), 0.0), 1.0) * 31.0;
    const int i = std::min(int(fq), 30), j = std::min(int(fm), 30);
    const double a = fq - i, b = fm - j;
    const double t0 = gTab[i * 32 + j] * (1 - b) + gTab[i * 32 + j + 1] * b;
    const double t1 = gTab[(i + 1) * 32 + j] * (1 - b) + gTab[(i + 1) * 32 + j + 1] * b;
    return t0 * (1 - a) + t1 * a;
}
static double widen(double alpha, double s, double mu, double &aW)   // s = the sphere's angular radius
{
    const double a = std::max(alpha, 0.001);
    const double ks = gK * s;
    aW = std::min(std::sqrt(a * a + ks * ks), 1.0);
    return lobeMuT(a, mu) / std::max(lobeMuT(aW, mu), 1e-6);
}

int main()
{
    const std::string path = JAH_FORK_HLMS_PBS_ANY "/JahBrdf_piece_all.any";
    if (!readShipped(path)) { std::printf("FAIL: the fork's JahBrdf (k and the table) unreadable: %s\n", path.c_str()); return 1; }
    std::printf("-- the shipped k %.3f and its 32 x 32 table of mu T (read from %s)\n", gK, path.c_str());
    const double rs[2] = { 0.05, 0.2 }, mus[2] = { 0.6, 0.9 }, ss[3] = { 0.01, 0.04, 0.1 };
    double worstK = 0.0;
    for (double r : rs)
        for (double mu : mus) {
            const double a = std::max(r * r, 0.001);
            // T over cos for the sphere's directions (a fine 1-D table, by the independent quadrature)
            const int nT = 200;
            std::vector<double> tT(nT + 1);
            for (int i = 0; i <= nT; ++i) tT[i] = T(a, std::max(1e-3, double(i) / nT), 400, 96);
            auto Tc = [&](double c) {
                const double f = std::min(std::max(c, 0.0), 1.0) * nT;
                const int i = std::min(int(f), nT - 1);
                return tT[i] * (1 - (f - i)) + tT[i + 1] * (f - i);
            };
            const double Ep = mu * T(a, mu);
            for (double s : ss) {
                // THE EXACT SPHERE: 4,096 stratified directions in the cone of half-angle s around the light's centre
                const V3 C{ std::sqrt(1.0 - mu * mu), 0.0, mu };
                const V3 tc = norm(cross(V3{ 0, 1, 0 }, C)), bc = cross(C, tc);
                const int m = 64;
                double acc = 0.0;
                for (int i = 0; i < m; ++i)
                    for (int j = 0; j < m; ++j) {
                        const double ct = 1.0 - (i + 0.5) / m * (1.0 - std::cos(s)), st = std::sqrt(1.0 - ct * ct);
                        const double ph = (j + 0.5) / m * 2.0 * kPi;
                        const V3 w = add(add(mul(tc, std::cos(ph) * st), mul(bc, std::sin(ph) * st)), mul(C, ct));
                        if (w.z > 0.0) acc += Tc(w.z) * w.z;
                    }
                const double omega = 2.0 * kPi * (1.0 - std::cos(s));
                const double Es = acc / (m * m) * omega / (kPi * std::sin(s) * std::sin(s));
                double aW;
                const double nrm = widen(a, s, mu, aW);
                const double Ew = mu * T(aW, mu) * nrm;
                // KARIS (the control): its energy integrated over V directly (the representative point per V)
                double Ek = 0.0;
                {
                    const double aK = std::min(a + std::sin(s) / 2.0, 1.0), nK = (a / aK) * (a / aK);
                    const V3 Rm{ -std::sqrt(1.0 - mu * mu), 0.0, mu };
                    const V3 t = norm(cross(V3{ 0, 1, 0 }, Rm)), b = cross(Rm, t);
                    const double W = 6.0 * s + 40.0 * a + 0.02;
                    const int n = 500;
                    const double du = 2.0 * W / n;
                    for (int i = 0; i < n; ++i)
                        for (int j = 0; j < n; ++j) {
                            const double U = -W + (i + 0.5) * du, Vv = -W + (j + 0.5) * du;
                            const double rho = std::sqrt(U * U + Vv * Vv);
                            const V3 V = norm(add(mul(Rm, std::cos(rho)),
                                                  mul(add(mul(t, U), mul(b, Vv)), rho > 1e-12 ? std::sin(rho) / rho : 1.0)));
                            const V3 R = add(mul(V3{ 0, 0, 1 }, 2.0 * V.z), mul(V, -1.0));
                            const V3 ctr = add(mul(R, dot(C, R)), mul(C, -1.0));
                            const double cl = std::sqrt(dot(ctr, ctr));
                            const V3 L = norm(add(C, mul(ctr, std::min(std::max(std::sin(s) / std::max(cl, 1e-20), 0.0), 1.0))));
                            Ek += lobe(L, V, a) * nK * mu * (rho > 1e-12 ? std::sin(rho) / rho : 1.0) * du * du;
                        }
                }
                std::printf("-- r %.2f cos %.1f R/d %.2f: point %.4f, EXACT SPHERE %.4f, widened %.4f (%+.2f %%), Karis %.4f "
                            "(%+.1f %%)\n", r, mu, s, Ep, Es, Ew, 100.0 * (Ew / Es - 1.0), Ek, 100.0 * (Ek / Es - 1.0));
                CHECK_MSG(std::fabs(Ew / Es - 1.0) <= 0.01, "THE LAW: r %.2f cos %.1f R/d %.2f - the widened glint's energy "
                          "the exact sphere's within 1 %% (%+.2f %%)", r, mu, s, 100.0 * (Ew / Es - 1.0));
                if (mu < 0.7) worstK = std::max(worstK, std::fabs(Ek / Es - 1.0));
            }
        }
    CHECK_MSG(worstK > 0.01, "THE NEGATIVE CONTROL: Karis's representative point is out of 1 %% at cos 0.6 (worst %.1f %%)",
              100.0 * worstK);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
