// frame.record_determinism — THE RECORD AND ITS SERIAL ARE DETERMINISTIC (ENGINE-UPGRADE-U3 stage 1; the one-thread arm).
//
// The render loop takes the record the document side accumulated at its frame head (the HAND-OVER) and stamps it
// with a serial THEN: one serial per frame that applied a record, never one per submit (decision F1 — the merge stays,
// it is the record's builder), and a frame nothing was submitted for is a legal EMPTY hand-over that carries the last
// serial (F2). Every test, every hash and the selftest run in LOCKSTEP — one turn of the loop, one hand-over, one
// frame — so the frames' inputs are the same byte sequence as before the loop existed.
//
// THE SCRIPT (60 frames, the same in both runs): a DOCUMENT node carrying a cube is moved by its node record every
// frame (the extract's path, Scene::createDocumentNode + NodeRecord), the record built at a document step that
// advances with the frame; every 7th frame the record is TWO submits (the host's several syncs: one hand-over), every
// 11th frame nothing is submitted (an empty hand-over: the cube holds its pose).
//
// THE LAWS, closed forms in frames:
//   * serial_k = the number of frames up to k whose hand-over carried a record (an empty one adds nothing), and
//     `recordApplied` is false on exactly the empty frames; every scene publishes the engine's serial;
//   * two submits before one frame are ONE hand-over (the serial moves by one) and one merge;
//   * the same script in TWO PROCESSES (Root is one per process) gives the byte-identical picture and the identical
//     serial sequence — the stage-4 arm runs this same script with the loop on its own thread and must read the same.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <sys/wait.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

namespace {

constexpr int kFrames = 60;
constexpr int kWarm = 4;
bool twoSubmits(int f) { return f % 7 == 3; }
bool noSubmit(int f) { return f % 11 == 5; }

FrameInput nodeRecord(Scene *scene, NodeId node, int f, int half)
{
    FrameInput in;
    in.frameDelta = 1.0f / 60.0f;
    in.stepSerial = (unsigned long long)(f);
    FrameInputScene part;
    part.scene = scene;
    NodeRecord r;
    r.id = node;
    r.fields = NodeRecord::Trs;
    const float t = float(f) + 0.5f * float(half);
    r.position = Vec3(0.6f * std::sin(t * 0.11f), 0.25f + 0.1f * std::cos(t * 0.07f), 0.0f);
    const float a = t * 0.05f;
    r.rotation = Quat(0.0f, std::sin(a * 0.5f), 0.0f, std::cos(a * 0.5f));   // (x, y, z, w)
    part.nodes.push_back(r);
    part.changes.moved.push_back(node);
    in.scenes.push_back(part);
    return in;
}

/// One run of the script in THIS process: prints the serial sequence and writes the picture's bytes to `out`.
int child(const char *out)
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-record-determinism-ogre.log"), err);
    if (!engine) { std::printf("CHILD-FAIL: engine create: %s\n", err.c_str()); return 2; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("record_determinism", 96, 96, Colour(0.05f, 0.06f, 0.08f, 1.0f));
    Scene *scene = view ? e->createScene("record_determinism") : nullptr;
    if (!view || !scene) { std::printf("CHILD-FAIL: view/scene: %s\n", e->lastError().c_str()); return 2; }
    view->setScene(scene);
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.55f), 3.14159f);
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(6.0f, 0.1f, 6.0f));
    // THE DOCUMENT NODE: its pose arrives through the record and nowhere else.
    const NodeId node = scene->createDocumentNode(reserveNodeId());   // the document reserves its ids (U3 stage 3, F6)
    const MeshId mesh = node ? scene->createMesh(enginetest::unitCubeMesh()) : 0;
    PbrParams p;
    p.albedo = Colour(0.85f, 0.2f, 0.15f);
    p.roughness = 0.5f;
    const MaterialId mat = mesh ? scene->createPbrMaterial(p) : 0;
    if (!mat || !scene->attachMesh(node, mesh, mat)) {
        std::printf("CHILD-FAIL: the document node's cube: %s\n", e->lastError().c_str());
        return 2;
    }
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(1.8f, 1.4f, 2.4f), Vec3(0, 0.25f, 0)));
    enginetest::frames(e, kWarm);

    const unsigned long long base = e->published().serial;
    const FrameSubmitStatus s0 = e->frameSubmitStatus();
    unsigned long long expected = base;
    int serialLaw = 0, appliedLaw = 0, sceneLaw = 0;
    std::string serials;
    for (int f = 0; f < kFrames; ++f) {
        if (!noSubmit(f)) {
            e->submitFrame(nodeRecord(scene, node, f, 0));
            if (twoSubmits(f)) e->submitFrame(nodeRecord(scene, node, f, 1));
            ++expected;
        }
        enginetest::frames(e, 1);
        const PublishedEngineFrame &pub = e->published();
        if (pub.serial == expected) ++serialLaw;
        if (pub.recordApplied == !noSubmit(f)) ++appliedLaw;
        if (scene->published().serial == pub.serial && scene->published().frame == pub.frame) ++sceneLaw;
        serials += std::to_string(pub.serial - base) + (f + 1 < kFrames ? "," : "");
    }
    const FrameSubmitStatus s1 = e->frameSubmitStatus();
    int submits = 0, merged = 0;
    for (int f = 0; f < kFrames; ++f)
        if (!noSubmit(f)) { submits += twoSubmits(f) ? 2 : 1; merged += twoSubmits(f) ? 1 : 0; }
    std::printf("CHILD serials=%s\n", serials.c_str());
    std::printf("CHILD serialLaw=%d appliedLaw=%d sceneLaw=%d submits=%llu merged=%llu (want %d, %d)\n", serialLaw,
                appliedLaw, sceneLaw, s1.submitted - s0.submitted, s1.scenePartsMerged - s0.scenePartsMerged,
                submits, merged);
    Image img;
    if (!view->readPixels(img) || img.rgba.empty()) { std::printf("CHILD-FAIL: readPixels\n"); return 2; }
    size_t lit = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
        if (img.rgba[i] > 40 && img.rgba[i] > img.rgba[i + 1] + 30) ++lit;   // the red cube's pixels
    std::printf("CHILD cubePixels=%zu\n", lit);
    std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char *>(img.rgba.data()),
                                               std::streamsize(img.rgba.size()));
    std::fflush(stdout);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    return 0;
}

std::string runChild(const char *self, const std::string &out, int &code)
{
    const std::string cmd = std::string("'") + self + "' --run '" + out + "' 2>&1";
    FILE *pipe = ::popen(cmd.c_str(), "r");
    std::string text;
    if (pipe) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0) text.append(buf, n);
    }
    const int status = pipe ? ::pclose(pipe) : -1;
    code = (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
    return text;
}

std::string line(const std::string &text, const char *key)
{
    const size_t at = text.find(key);
    if (at == std::string::npos) return std::string();
    const size_t end = text.find('\n', at);
    return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

std::vector<char> slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

}   // namespace

int main(int argc, char **argv)
{
    if (argc > 2 && std::strcmp(argv[1], "--run") == 0) return child(argv[2]);

    int codeA = -1, codeB = -1;
    const std::string outA = "record_determinism_a.rgba", outB = "record_determinism_b.rgba";
    const std::string a = runChild(argv[0], outA, codeA);
    const std::string b = runChild(argv[0], outB, codeB);
    CHECK(codeA == 0 && codeB == 0, "both runs of the script finished (exit %d / %d)", codeA, codeB);
    if (codeA != 0) std::printf("--- run A ---\n%s\n", a.c_str());
    if (codeB != 0) std::printf("--- run B ---\n%s\n", b.c_str());
    const std::string lawA = line(a, "CHILD serialLaw="), lawB = line(b, "CHILD serialLaw=");
    std::printf("    run A: %s\n    run B: %s\n    %s\n", lawA.c_str(), lawB.c_str(), line(a, "CHILD cubePixels=").c_str());
    char want[160];
    int submits = 0, merged = 0;
    for (int f = 0; f < kFrames; ++f)
        if (!noSubmit(f)) { submits += twoSubmits(f) ? 2 : 1; merged += twoSubmits(f) ? 1 : 0; }
    std::snprintf(want, sizeof want, "CHILD serialLaw=%d appliedLaw=%d sceneLaw=%d submits=%d merged=%d", kFrames,
                  kFrames, kFrames, submits, merged);
    CHECK(lawA.rfind(want, 0) == 0,
          "ONE SERIAL PER FRAME THAT APPLIED A RECORD: %d frames, the serial = the non-empty hand-overs, "
          "`recordApplied` false on the empty frames only, every scene publishing the engine's serial, "
          "%d submits merged into %d hand-overs (%s)", kFrames, submits, submits - merged, lawA.c_str());
    const std::string serA = line(a, "CHILD serials="), serB = line(b, "CHILD serials=");
    CHECK(!serA.empty() && serA == serB, "THE SAME SCRIPT TWICE: the identical serial sequence (%s)", serA.c_str());
    const std::vector<char> pixA = slurp(outA), pixB = slurp(outB);
    CHECK(!pixA.empty() && pixA == pixB, "...and the byte-identical picture (%zu bytes)", pixA.size());
    const std::string cube = line(a, "CHILD cubePixels=");
    CHECK(!cube.empty() && cube != "CHILD cubePixels=0", "the document node's cube is drawn (%s)", cube.c_str());
    std::remove(outA.c_str());
    std::remove(outB.c_str());
    std::printf("frame.record_determinism: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
