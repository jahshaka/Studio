// shadergraph.bake_output — pixel tests over the per-texel baker's PNGs
// (SPECS/MATERIALS_EVALUATOR_SPEC.md sections 1.3, 1.6, 2, 6).
//
// Probes: texCoords->splitvector U->Roughness renders a horizontal gradient
// (corner/center bytes within 1/255); a step/fraction checker; a
// textureSampler x color tint of a 2x2 fixture; a varying alpha chain packs
// into baseColorMap.A with alphaMode=2; normal passthrough binds the source
// byte-identically; a second bake is a cache hit (same hash-named file,
// mtime untouched); stale files prune; and the section-2 perf budgets
// (preview 256^2 of a 20-node graph, final 1024^2) are measured and gated
// generously for lavapipe-class CI boxes.
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QThread>
#include <cmath>
#include <cstdio>

#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/connectionmodel.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"
#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/pbrgraphevaluator.h"

using materials::GraphBaker;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool nearByte(int a, int b) { return std::abs(a - b) <= 1; } // +-1/255

struct Rig
{
    NodeGraph* graph;
    NodeModel* master;
    LibraryV1* lib;

    Rig()
    {
        lib = new LibraryV1();
        graph = new NodeGraph();
        graph->setNodeLibrary(lib);
        master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);
    }
    NodeModel* add(const QString& type)
    {
        auto node = lib->createNode(type);
        graph->addNode(node);
        return node;
    }
    NodeModel* addFloat(double v)
    {
        auto node = add("float");
        node->deserializeWidgetValue(QJsonValue(v));
        return node;
    }
    NodeModel* addColor(double r, double g, double b, double a = 1.0)
    {
        auto node = add("color");
        QJsonObject obj;
        obj["r"] = r; obj["g"] = g; obj["b"] = b; obj["a"] = a;
        node->deserializeWidgetValue(obj);
        return node;
    }
    void toMaster(NodeModel* from, int out, int in) { graph->addConnection(from, out, master, in); }
};

static GraphBaker::Result bake(Rig& rig, const QString& dir, int resolution, double time = 0.0)
{
    GraphBaker::Options opts;
    opts.resolution = resolution;
    opts.time = time;
    opts.outputDir = dir;
    opts.relativePrefix = dir + "/"; // absolute values: no resolver needed in tests
    return GraphBaker::run(rig.graph, opts);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString baseDir = QDir::current().absoluteFilePath("bake_output_scratch");
    QDir(baseDir).removeRecursively();
    QDir().mkpath(baseDir);

    // ---- 1. horizontal gradient: texCoords -> split U -> Roughness ---------
    QString gradientFile;
    {
        Rig r;
        auto split = r.add("splitvector");
        r.graph->addConnection(r.add("texCoords"), 0, split, 0);
        r.toMaster(split, 0, 2); // X -> Roughness

        const QString dir = baseDir + "/gradient";
        auto res = bake(r, dir, 64);
        CHECK(res.maps.contains("roughnessMap"), "gradient: a roughnessMap was baked");
        CHECK(res.eval.values["roughness"].toDouble() == 1.0,
              "gradient: roughness factor lands 1.0 (the engine multiplies map x factor)");
        gradientFile = res.eval.values["roughnessMap"].toString();
        QImage img(gradientFile);
        img = img.convertToFormat(QImage::Format_RGBA8888);
        CHECK(img.width() == 64 && img.height() == 64, "gradient: 64x64 PNG on disk");
        auto gray = [&](int x, int y) { return qRed(img.pixel(x, y)); };
        CHECK(nearByte(gray(0, 0), int(std::lround(0.5 / 64 * 255))), "gradient: left column ~ 0 (+-1/255)");
        CHECK(nearByte(gray(32, 10), int(std::lround(32.5 / 64 * 255))), "gradient: center column ~ 129 (+-1/255)");
        CHECK(nearByte(gray(63, 63), int(std::lround(63.5 / 64 * 255))), "gradient: right column ~ 253 (+-1/255)");
        CHECK(gray(5, 0) == gray(5, 32) && gray(40, 1) == gray(40, 63),
              "gradient: horizontal - value independent of y");
        // grayscale maps write the value to R, G and B
        CHECK(qGreen(img.pixel(32, 10)) == gray(32, 10) && qBlue(img.pixel(32, 10)) == gray(32, 10),
              "gradient: grayscale written to RGB");
    }

    // ---- 2. step/fraction checker ------------------------------------------
    {
        Rig r;
        auto split = r.add("splitvector");
        r.graph->addConnection(r.add("texCoords"), 0, split, 0);
        auto mul = r.add("multiply");
        r.graph->addConnection(split, 0, mul, 0);
        r.graph->addConnection(r.addFloat(2.0), 0, mul, 1);
        auto frac = r.add("fraction");
        r.graph->addConnection(mul, 0, frac, 0);
        auto step = r.add("step"); // Edge, Value
        r.graph->addConnection(r.addFloat(0.5), 0, step, 0);
        r.graph->addConnection(frac, 0, step, 1);
        r.toMaster(step, 0, 2);

        auto res = bake(r, baseDir + "/checker", 8);
        QImage img(res.eval.values["roughnessMap"].toString());
        img = img.convertToFormat(QImage::Format_RGBA8888);
        auto gray = [&](int x) { return qRed(img.pixel(x, 4)); };
        CHECK(gray(0) == 0 && gray(1) == 0, "checker: band 1 (u<0.25) is black");
        CHECK(gray(2) == 255 && gray(3) == 255, "checker: band 2 is white");
        CHECK(gray(4) == 0 && gray(5) == 0, "checker: band 3 is black");
        CHECK(gray(6) == 255 && gray(7) == 255, "checker: band 4 is white");
    }

    // ---- 3. textureSampler x color tint of a 2x2 fixture -------------------
    {
        const QString texPath = baseDir + "/fixture2x2.png";
        QImage fix(2, 2, QImage::Format_RGBA8888);
        fix.setPixelColor(0, 0, QColor(255, 0, 0));
        fix.setPixelColor(1, 0, QColor(0, 255, 0));
        fix.setPixelColor(0, 1, QColor(0, 0, 255));
        fix.setPixelColor(1, 1, QColor(255, 255, 255));
        CHECK(fix.save(texPath), "tint: 2x2 fixture written");

        Rig r;
        auto tex = r.add("texture");
        static_cast<TextureNode*>(tex)->setTexturePath(texPath);
        auto sampler = r.add("textureSampler");
        r.graph->addConnection(tex, 0, sampler, 0);
        r.graph->addConnection(r.add("texCoords"), 0, sampler, 1);
        auto mul = r.add("multiply");
        r.graph->addConnection(sampler, 0, mul, 0);
        r.graph->addConnection(r.addColor(0.5, 0.5, 0.5), 0, mul, 1);
        r.toMaster(mul, 0, 0); // Base Color

        auto res = bake(r, baseDir + "/tint", 2);
        CHECK(res.maps.contains("baseColorMap"), "tint: baseColorMap baked (math breaks passthrough)");
        QImage img(res.eval.values["baseColorMap"].toString());
        img = img.convertToFormat(QImage::Format_RGBA8888);
        CHECK(img.width() == 2 && img.height() == 2, "tint: 2x2 output at bake resolution 2");
        // texel centers sample exactly; every channel is halved
        CHECK(nearByte(qRed(img.pixel(0, 0)), 128) && qGreen(img.pixel(0, 0)) == 0,
              "tint: (255,0,0) x 0.5 == ~(128,0,0)");
        CHECK(nearByte(qGreen(img.pixel(1, 0)), 128), "tint: (0,255,0) x 0.5 == ~(0,128,0)");
        CHECK(nearByte(qBlue(img.pixel(0, 1)), 128), "tint: (0,0,255) x 0.5 == ~(0,0,128)");
        CHECK(nearByte(qRed(img.pixel(1, 1)), 128) && nearByte(qBlue(img.pixel(1, 1)), 128),
              "tint: white x 0.5 == ~gray");
        CHECK(!res.eval.values.contains("baseColor"),
              "tint: no baseColor value beside the baked map (no double-multiply)");
    }

    // ---- 4. varying alpha packs into baseColorMap.A ------------------------
    {
        Rig r;
        r.toMaster(r.addColor(1.0, 0.0, 0.0), 0, 0); // uniform red base
        auto split = r.add("splitvector");
        r.graph->addConnection(r.add("texCoords"), 0, split, 0);
        r.toMaster(split, 0, 6); // U -> Alpha (varying)

        auto res = bake(r, baseDir + "/alpha", 4);
        CHECK(res.maps.contains("baseColorMap"),
              "alpha: varying alpha forces baseColorMap synthesis over a uniform base");
        CHECK(res.eval.values["alphaMode"].toInt() == 2, "alpha: alphaMode=2 (blend) with no cutoff");
        CHECK(!res.eval.values.contains("alpha"), "alpha: no scalar alpha beside the baked A channel");
        CHECK(!res.eval.values.contains("baseColor"), "alpha: folded base colour moved into the map RGB");
        QImage img(res.eval.values["baseColorMap"].toString());
        img = img.convertToFormat(QImage::Format_RGBA8888);
        CHECK(qRed(img.pixel(0, 0)) == 255 && qGreen(img.pixel(0, 0)) == 0,
              "alpha: RGB filled with the folded base colour");
        CHECK(nearByte(qAlpha(img.pixel(0, 0)), int(std::lround(0.5 / 4 * 255))),
              "alpha: A(x=0) ~ 32 (the alpha chain per texel)");
        CHECK(nearByte(qAlpha(img.pixel(3, 2)), int(std::lround(3.5 / 4 * 255))),
              "alpha: A(x=3) ~ 223");
    }

    // ---- 5. normal passthrough is byte-identical (source bound directly) ---
    {
        const QString texPath = baseDir + "/normal_src.png";
        QImage n(4, 4, QImage::Format_RGBA8888);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                n.setPixelColor(x, y, QColor(128 + x, 128 + y, 255 - x));
        n.save(texPath);

        Rig r;
        auto tex = r.add("texture");
        static_cast<TextureNode*>(tex)->setTexturePath(texPath);
        r.toMaster(tex, 0, 3); // Normal

        auto res = bake(r, baseDir + "/normal", 16);
        CHECK(res.passthrough["normalMap"].toString() == texPath,
              "normal: passthrough binds the source file itself (no bake, no copy)");
        CHECK(res.eval.values["normalMap"].toString() == texPath,
              "normal: the map value IS the source path - byte-identical by construction");
        CHECK(res.maps.isEmpty(), "normal: nothing was baked for a bare texture chain");
    }

    // ---- 6. cache hit: second bake reuses the hash-named file --------------
    {
        Rig r;
        auto split = r.add("splitvector");
        r.graph->addConnection(r.add("texCoords"), 0, split, 0);
        r.toMaster(split, 0, 2);

        const QString dir = baseDir + "/cache";
        auto first = bake(r, dir, 32);
        const QString file = first.eval.values["roughnessMap"].toString();
        const QDateTime mtime = QFileInfo(file).lastModified();
        QThread::msleep(1100); // mtime granularity
        auto second = bake(r, dir, 32);
        CHECK(second.eval.values["roughnessMap"].toString() == file,
              "cache: identical graph re-bake emits the same hash-named file");
        CHECK(QFileInfo(file).lastModified() == mtime,
              "cache: the PNG was not rewritten (mtime unchanged)");

        // a value edit changes the hash and prunes the stale file
        auto mul = r.add("multiply");
        // rewire: split.X -> multiply x 0.5 -> Roughness
        // (remove the old master connection by connecting a new chain)
        auto cons = r.graph->getNodeConnections(r.master->id);
        for (auto con : cons) r.graph->removeConnection(con->id);
        r.graph->addConnection(split, 0, mul, 0);
        r.graph->addConnection(r.addFloat(0.5), 0, mul, 1);
        r.toMaster(mul, 0, 2);
        auto third = bake(r, dir, 32);
        const QString newFile = third.eval.values["roughnessMap"].toString();
        CHECK(newFile != file, "cache: an edited chain bakes under a new hash");
        CHECK(!QFileInfo::exists(file), "cache: the stale map was pruned");
        CHECK(QFileInfo::exists(newFile), "cache: the new map exists");
    }

    // ---- 7. classification via bakeInfo around a real bake -----------------
    {
        Rig r;
        auto split = r.add("splitvector");
        r.graph->addConnection(r.add("texCoords"), 0, split, 0);
        r.toMaster(split, 0, 2);
        r.toMaster(r.addFloat(0.3), 0, 1);
        auto info = GraphBaker::classify(r.graph)["perSocket"].toObject();
        CHECK(info["Roughness"].toString() == "baked" && info["Metallic"].toString() == "uniform",
              "classify: baked vs uniform reported per socket");
    }

    // ---- 8. perf budgets (spec section 2, generous for CI) -----------------
    {
        Rig r;
        // a ~20-node varying graph: texCoords -> split -> 16 alternating ops -> Roughness
        auto split = r.add("splitvector");
        r.graph->addConnection(r.add("texCoords"), 0, split, 0);
        NodeModel* head = split;
        int headOut = 0;
        const char* chain[] = { "add", "multiply", "fraction", "oneminus",
                                "add", "multiply", "fraction", "oneminus",
                                "add", "multiply", "fraction", "oneminus",
                                "add", "multiply", "fraction", "oneminus" };
        for (const char* type : chain) {
            auto op = r.add(type);
            r.graph->addConnection(head, headOut, op, 0);
            head = op;
            headOut = 0;
        }
        r.toMaster(head, 0, 2);

        auto preview = bake(r, baseDir + "/perf_preview", 256);
        std::printf("perf: preview bake 256^2, 20-node graph: %lld ms (budget 50, gate 150)\n",
                    (long long)preview.msElapsed);
        CHECK(preview.msElapsed <= 150, "perf: preview 256^2 within the (generous) gate");

        auto final1k = bake(r, baseDir + "/perf_final", 1024);
        std::printf("perf: final bake 1024^2: %lld ms (budget 2000, gate 6000)\n",
                    (long long)final1k.msElapsed);
        CHECK(final1k.msElapsed <= 6000, "perf: final 1024^2 within the (generous) gate");
    }

    QDir(baseDir).removeRecursively();
    std::printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
