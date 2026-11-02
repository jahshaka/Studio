// gi.card_law (BISTRO-REST-1, the lead's ruling (3)) - A CARD TEXEL NEVER SENDS NEGATIVE LIGHT, AND ITS MOVE RULE READS
// EVERY VALID REFERENCE AS ONE.
// Bistro Exterior at Epic never rested (spikes/bistro-rest-1/stage1/cards-census): six cards held a texel whose radiance
// was negative in all three channels (fp16 subnormals, -1.8e-7 .. -3.4e-5). The relight's move rule read a negative
// last-reported value as "no reference yet" and reported a move of 3e38 at every march - every reader of the card listed
// at infinite codes, three of the six reading themselves: the cards relit 16 a frame for good, and no reader rested.
// The negative came from the card READ's lobe cut (C2: a reader tracing the hit's lobe leaves the stored lobe share out):
// it cut a share recomputed WITHOUT the texel's open fraction from a flat term that carries it.
// THIS ROW RUNS THE SHIPPED TEXT (Engine::giLaw(GiLaw::Card): rq_card_law.comp calls jah_card_view.glsl's
// jahCardStoreTerms - the relight's stored terms -, jahCardReadView - every ray job's card read - and jahCardMoveReport
// - the relight's move -, the layers stored as the relight stores them: rounded to nearest, then the half-float store's
// truncation) over a fixture texel - a metal, a dielectric, a dark dielectric and a tinted metal, the open fraction over
// the half float's grid in (0, 1], the irradiance 1e-7 .. 1e3, the roughness 0.05 .. 1, with and without an emissive and
// a direct half - and holds:
//   (i)   THE READ IS NEVER NEGATIVE, toward any of three directions, and the cut's residual F - cut BEFORE any clamp is
//         never below half a half-float step of the flat term (the one rounding of F; 2^-24 among the denormals);
//   (ii)  ANY FINITE LAST-REPORTED VALUE IS A REFERENCE: no move of 3e38 from a valid one, negatives included - the move is
//         |S - ref| and the report its code test;
//   (iii) A RE-MARCH OF AN UNCHANGED TEXEL REPORTS NOTHING - the texel itself, and a receiver whose gather read it.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-card-law-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    View *view = e->createOffscreenView("cardlaw", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("cardlaw");
    view->setScene(scene);
    enginetest::frames(e, 2);   // (the ray tier opens in a frame)

    // ---- (i) and (iii): THE TEXEL -------------------------------------------------------------------------------------
    struct Texel { float irr, open, rough, E; unsigned mat, direct; };
    std::vector<Texel> texels;
    std::vector<GiLawQuestion> qs;
    const float roughs[4] = { 0.05f, 0.3f, 0.6f, 1.0f };
    const float Es[2] = { 0.0f, 1e-3f };
    for (unsigned mat = 0; mat < 4u; ++mat)
        for (unsigned direct = 0; direct < 2u; ++direct)
            for (float E : Es)
                for (float rough : roughs)
                    for (int o = 1; o <= 64; ++o)   // the open fraction on the half float's grid, 1/64 .. 1
                        for (double li = std::log2(1e-7); li <= std::log2(1e3); li += 0.5) {
                            const Texel t{ float(std::exp2(li)), float(o) / 64.0f, rough, E, mat, direct };
                            texels.push_back(t);
                            GiLawQuestion q;
                            q.q[0] = t.irr; q.q[1] = t.open; q.q[2] = t.rough; q.q[3] = t.E;
                            q.u[0] = 0u; q.u[1] = mat; q.u[2] = direct;
                            qs.push_back(q);
                        }
    // ---- (ii): THE REFERENCE ------------------------------------------------------------------------------------------
    struct Ref { float S, ref; };
    std::vector<Ref> refs;
    for (double ls = std::log2(1e-7); ls <= std::log2(1e3); ls += 1.0)
        for (double lr = std::log2(1e-7); lr <= std::log2(1e3); lr += 1.0)
            for (int sg = 0; sg < 4; ++sg) {
                const float S = float(std::exp2(ls)) * ((sg & 1) ? -1.0f : 1.0f);
                const float ref = float(std::exp2(lr)) * ((sg & 2) ? -1.0f : 1.0f);
                refs.push_back({ S, ref });
                GiLawQuestion q;
                q.q[0] = S; q.q[1] = ref;
                q.u[0] = 1u;
                qs.push_back(q);
            }
    std::vector<GiLawAnswer> as;
    if (!e->giLaw(GiLaw::Card, qs, as) || as.size() != qs.size()) {
        std::printf("FAIL: giLaw(Card): %s\n", e->takeLastError().c_str());
        return 1;
    }
    static const char *kMat[4] = { "metal", "dielectric", "dark dielectric", "tinted metal" };
    int fails = 0;
    {
        size_t negative = 0, residualOut = 0, rereports = 0, receiverRereports = 0;
        double least = 0.0, worstResidual = 0.0;
        size_t atLeast = 0, atResidual = 0, atRe = 0, atRecv = 0;
        for (size_t j = 0; j < texels.size(); ++j) {
            const GiLawAnswer &a = as[j];
            if (a.a[0] < 0.0f) { ++negative; if (a.a[0] < least) { least = a.a[0]; atLeast = j; } }
            // (the bound: half a step of F's own rounding - its denormals' whole 2^-24 step, the store truncating there)
            const double bound = a.b[2] < 6.1035156e-5f ? -1.0 : -0.5;
            if (a.a[1] < bound) { ++residualOut; if (a.a[1] < worstResidual) { worstResidual = a.a[1]; atResidual = j; } }
            if (a.a[2] != 0.0f) { if (!rereports) atRe = j; ++rereports; }
            if (a.a[3] != 0.0f) { if (!receiverRereports) atRecv = j; ++receiverRereports; }
        }
        const auto describe = [&](size_t j) {
            const Texel &t = texels[j];
            char b[200];
            std::snprintf(b, sizeof b, "%s, irradiance %.3g, open %.4f, roughness %.2f, emissive %.0e, %s", kMat[t.mat], t.irr,
                          t.open, t.rough, t.E, t.direct ? "a direct half" : "in shade");
            return std::string(b);
        };
        const bool okI = negative == 0 && residualOut == 0;
        std::printf("%s (i) %zu texels: %zu read negative (the least %.4g: %s); %zu cut residuals past the flat term's one "
                    "rounding (the worst %.2f steps: %s) - bars 0 / 0\n", okI ? "ok:" : "FAIL:", texels.size(), negative,
                    least, negative ? describe(atLeast).c_str() : "-", residualOut, worstResidual,
                    residualOut ? describe(atResidual).c_str() : "-");
        const bool okIII = rereports == 0 && receiverRereports == 0;
        std::printf("%s (iii) unchanged re-marches: %zu texels reported (first: %s), %zu receivers reported (first: %s, its "
                    "move %.3g, its S %.4g) - bars 0 / 0\n", okIII ? "ok:" : "FAIL:", rereports,
                    rereports ? describe(atRe).c_str() : "-", receiverRereports,
                    receiverRereports ? describe(atRecv).c_str() : "-", receiverRereports ? as[atRecv].b[0] : 0.0f,
                    receiverRereports ? as[atRecv].b[1] : 0.0f);
        fails += !okI + !okIII;
    }
    {
        size_t sentinel = 0, wrongMove = 0, wrongReport = 0;
        const Ref *first = nullptr;
        for (size_t j = 0; j < refs.size(); ++j) {
            const GiLawAnswer &a = as[texels.size() + j];
            const Ref &R = refs[j];
            const double d = std::fabs(double(R.S) - R.ref);
            const double code = 0.01 * std::max(std::max(double(R.S), double(R.ref)), 5.9604645e-6);
            if (a.a[0] >= 1e38f) { ++sentinel; if (!first) first = &R; }
            else if (std::fabs(a.a[0] - d) > 1e-6 * std::max(d, 1e-30)) ++wrongMove;
            if ((a.a[1] != 0.0f) != (d > code)) ++wrongReport;
        }
        const bool okII = sentinel == 0 && wrongMove == 0 && wrongReport == 0;
        std::printf("%s (ii) %zu finite references: %zu read as none (a 3e38 move; first S %.3g ref %.3g), %zu moves not "
                    "|S - ref|, %zu reports not its code test - bars 0 / 0 / 0\n", okII ? "ok:" : "FAIL:", refs.size(), sentinel,
                    first ? first->S : 0.0f, first ? first->ref : 0.0f, wrongMove, wrongReport);
        fails += !okII;
    }
    std::printf("gi.card_law: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
