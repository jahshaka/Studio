// GPU_SKINNING_SPEC phase 3 / T3, T4, T5, T6 — THE gate.
//
// T3 parity: the same rig, the same pose, rendered twice — once through the CPU
// skinner (skinVertices + updateMeshVertices, the path that shipped) and once
// through the GPU one (attachSkinnedMesh + setBonePoses) — must produce the same
// pixels. The CPU path is retained as a document-side ORACLE for exactly this;
// nothing renders through it any more.
//
// T4: two avatars of ONE mesh asset, posed differently, in ONE frame. The CPU
// path could not do this at all (one engine mesh per mesh asset means one vertex
// buffer to fight over) — it is the reason this program exists.
//
// T5: tangents. The CPU skinner skins position and normal only, so a
// normal-mapped character kept its BIND tangent frame and lit wrongly as it
// deformed. Ogre's vertex shader skins the tangent too. This is a case where the
// two paths are expected to DIFFER, and the GPU one is the correct one.
//
// T6: the numbers. Non-failing print of ms/frame and bytes uploaded for both
// paths at 1, 4 and 8 characters of ~50k vertices.
#include <QGuiApplication>
#include <QElapsedTimer>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/document/scenegraph/scene.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenemirror.h"
#include "armrig.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void render(Engine *e, int frames = 3) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

// The pose, in closed form. It used to come from the document evaluator
// (updateSceneAnimation -> Skeleton::boneTransforms -> toBonePoses); that
// evaluator is retired (ANIMATION_ENGINE_MIGRATION_SPEC), and an analytic pose
// is the better oracle anyway — it cannot drift with the thing it checks, and
// skeletal.rig_translation proves the engine reproduces exactly these matrices.
static std::vector<BonePose> armPoses(float t)
{
    const QVector<armrig::ArmPose> local = armrig::swingLocalPoses(-90.0f, t);
    std::vector<BonePose> out(size_t(local.size()));
    for (int i = 0; i < local.size(); ++i) {
        out[size_t(i)].position = Vec3(local[i].pos.x(), local[i].pos.y(), local[i].pos.z());
        out[size_t(i)].rotation = Quat(local[i].rot.x(), local[i].rot.y(),
                                       local[i].rot.z(), local[i].rot.scalar());
        out[size_t(i)].scale = Vec3(local[i].scale.x(), local[i].scale.y(), local[i].scale.z());
    }
    return out;
}

// ---- the fixture ----------------------------------------------------------
// A skinned strip: `rows` x 2 vertices spanning y in [0,2], the top half
// weighted to bone 1 ("jointTip", bind at y=1) and the bottom to bone 0, with a
// linear blend across the middle so most vertices carry two influences.
struct Strip { iris::MeshPtr mesh; MeshData cpuData, gpuData; };
static Strip buildStrip(int rows)
{
    Strip out;
    MeshData d;
    const float hw = 0.15f;
    for (int r = 0; r < rows; ++r) {
        const float y = 2.0f * float(r) / float(rows - 1);
        for (int c = 0; c < 2; ++c) {
            d.positions.insert(d.positions.end(), { c ? hw : -hw, y, 0.0f });
            d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
            d.uvs.insert(d.uvs.end(), { c ? 1.0f : 0.0f, y * 0.5f });
            // Weight: 0 below y=0.8, ramping to bone 1 by y=1.2.
            const float w1 = std::min(1.0f, std::max(0.0f, (y - 0.8f) / 0.4f));
            d.blendIndices.insert(d.blendIndices.end(), { 0, 1, 0, 0 });
            d.blendWeights.insert(d.blendWeights.end(), { 1.0f - w1, w1, 0.0f, 0.0f });
        }
        if (r + 1 < rows) {
            const unsigned b = unsigned(r * 2);
            d.indices.insert(d.indices.end(), { b, b + 1, b + 3, b, b + 3, b + 2 });
        }
    }
    out.gpuData = d;
    out.cpuData = d;
    out.cpuData.blendIndices.clear();
    out.cpuData.blendWeights.clear();
    out.cpuData.dynamic = true;
    return out;
}

// The document half: an iris mesh carrying the same skeleton and the same
// per-vertex bone data, so the CPU skinner and the descriptor see one rig.
static iris::MeshPtr buildStripDocMesh(const MeshData &d)
{
    auto mesh = iris::Mesh::create();
    std::vector<float> bi(d.blendIndices.size()), bw = d.blendWeights;
    for (size_t i = 0; i < bi.size(); ++i) bi[i] = float(d.blendIndices[i]);
    auto addBuf = [&mesh](iris::VertexAttribUsage usage, const float *data, size_t floats, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(data), unsigned(floats * sizeof(float)));
        mesh->addVertexBuffer(vb);
    };
    addBuf(iris::VertexAttribUsage::Position,    d.positions.data(), d.positions.size(), 3);
    addBuf(iris::VertexAttribUsage::Normal,      d.normals.data(),   d.normals.size(),   3);
    addBuf(iris::VertexAttribUsage::TexCoord0,   d.uvs.data(),       d.uvs.size(),       2);
    addBuf(iris::VertexAttribUsage::BoneIndices, bi.data(),          bi.size(),          4);
    addBuf(iris::VertexAttribUsage::BoneWeights, bw.data(),          bw.size(),          4);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(d.indices.data()), unsigned(d.indices.size() * sizeof(unsigned)));
    mesh->setIndexBuffer(ib);
    mesh->setVertexCount(int(d.vertexCount()));

    auto skel = iris::Skeleton::create();
    auto root = iris::Bone::create("jointRoot");
    auto tip = iris::Bone::create("jointTip");
    tip->meshSpacePoseMatrix.translate(0, 1, 0);
    tip->inverseMeshSpacePoseMatrix.translate(0, -1, 0);
    skel->addBone(root);
    skel->addBone(tip);
    root->addChild(tip);
    mesh->setSkeleton(skel);
    return mesh;
}

// A document scene holding the strip node + its bone nodes + the swing clip.
struct Doc { iris::ScenePtr scene; iris::MeshNodePtr node; };
static Doc buildDoc(const iris::MeshPtr &mesh)
{
    Doc d;
    d.scene = iris::Scene::create();
    d.node = armrig::buildArmNode(mesh, "strip");
    auto clip = armrig::buildSwingClip(-90.0f);
    d.node->addAnimation(clip); d.node->setAnimation(clip);
    d.scene->getRootNode()->addChild(d.node);
    return d;
}

static int maxChannelDiff(const Image &a, const Image &b, int &differing)
{
    int worst = 0; differing = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            const int d = std::max({ int(std::lround(std::fabs(double(ca.r - cb.r)) * 255.0)),
                                     int(std::lround(std::fabs(double(ca.g - cb.g)) * 255.0)),
                                     int(std::lround(std::fabs(double(ca.b - cb.b)) * 255.0)) });
            if (d) ++differing;
            worst = std::max(worst, d);
        }
    return worst;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_gpu_parity-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // ================= T3: parity =================
    {
        Strip strip = buildStrip(24);
        auto docMesh = buildStripDocMesh(strip.gpuData);
        Doc doc = buildDoc(docMesh);

        // Two scenes, two views, one camera and one light setup — the only
        // difference between them is WHERE the deformation runs.
        auto makeView = [&](const char *name, View *&v, Scene *&sc) {
            v = engine->createOffscreenView(name, 128, 128, Colour(0, 0, 1));
            sc = engine->createScene(name);
            v->setScene(sc);
            sc->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
            CameraDesc cam; cam.position = Vec3(0, 1, 5);
            v->setCamera(cam);
        };
        View *vCpu, *vGpu; Scene *sCpu, *sGpu;
        makeView("parity-cpu", vCpu, sCpu);
        makeView("parity-gpu", vGpu, sGpu);

        PbrParams mp; mp.albedo = Colour(0, 0, 0); mp.emissive = Colour(1, 0, 0);
        MaterialId mCpu = sCpu->createPbrMaterial(mp);
        MaterialId mGpu = sGpu->createPbrMaterial(mp);

        MeshId cpuMesh = sCpu->createMesh(strip.cpuData);
        MeshId gpuMesh = sGpu->createMesh(strip.gpuData);
        NodeId nCpu = sCpu->createNode();
        NodeId nGpu = sGpu->createNode();
        CHECK(sCpu->attachMesh(nCpu, cpuMesh, mCpu), "CPU-path mesh attached");

        SkeletonDesc rig;
        CHECK(SceneMirror::toSkeletonDesc(doc.node->getSkeleton(), rig), "rig descriptor built");
        CHECK(sGpu->attachSkinnedMesh(nGpu, gpuMesh, mGpu, rig), "GPU-path mesh attached");

        std::vector<float> bi, bw;
        SceneMirror::toSkinData(docMesh.data(), bi, bw);

        int worstOverall = 0;
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 0.95f }) {
            // CPU: skin every vertex with the ANALYTIC skin matrices and
            // re-upload the whole buffer.
            std::vector<float> pos, nrm;
            SceneMirror::skinVertices(armrig::swingSkinMatrices(-90.0f, t),
                                      strip.cpuData.positions, strip.cpuData.normals,
                                      bi, bw, pos, nrm);
            CHECK(sCpu->updateMeshVertices(cpuMesh, pos, nrm) || t < 0, "CPU vertices pushed");

            // GPU: push the same pose as parent-local bone TRS.
            std::vector<BonePose> poses = armPoses(t);
            CHECK(sGpu->setBonePoses(nGpu, poses.data(), poses.size()) || t < 0, "GPU pose pushed");

            render(engine.get());
            Image a, b;
            vCpu->readPixels(a); vGpu->readPixels(b);
            int differing = 0;
            const int worst = maxChannelDiff(a, b, differing);
            worstOverall = std::max(worstOverall, worst);
            std::printf("    t=%.2f  worst channel diff %d/255, differing pixels %d/%u\n",
                        double(t), worst, differing, a.width * a.height);
        }
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "T3: GPU and CPU skinning agree to %d/255 across the whole swing", worstOverall);
        CHECK(worstOverall <= 2, msg);

        // Sanity: the picture actually MOVED across the swing, so parity is not
        // "both paths rendered nothing".
        std::vector<BonePose> p0 = armPoses(0.0f);
        sGpu->setBonePoses(nGpu, p0.data(), p0.size());
        render(engine.get());
        Image bind; vGpu->readPixels(bind);
        std::vector<BonePose> p5 = armPoses(0.5f);
        sGpu->setBonePoses(nGpu, p5.data(), p5.size());
        render(engine.get());
        Image bent; vGpu->readPixels(bent);
        int moved = 0;
        CHECK(maxChannelDiff(bind, bent, moved) > 32, "T3: the swing actually moves pixels");
        std::printf("    pixels changed by the swing: %d\n", moved);

        engine->destroyView(vCpu); engine->destroyScene(sCpu);
        engine->destroyView(vGpu); engine->destroyScene(sGpu);
    }

    // ================= T4: two avatars, one mesh, one frame =================
    {
        Strip strip = buildStrip(24);
        auto docMesh = buildStripDocMesh(strip.gpuData);
        auto scene = iris::Scene::create();
        auto a = armrig::buildArmNode(docMesh, "a");
        auto b = armrig::buildArmNode(docMesh, "b");
        a->setLocalPos(QVector3D(-1.2f, 0, 0));
        b->setLocalPos(QVector3D( 1.2f, 0, 0));
        // TWO clips, one per avatar. Since the clip evaluator moved to the
        // engine the document states {which clip, one absolute clock} and the
        // engine samples it, so "pose A at 0.5 while B stays at 0" is no longer
        // expressible by poking one node's updateAnimation — and it never should
        // have been, that was reaching past the scene's clock. The claim T4
        // actually makes — two nodes on ONE mesh asset hold two independent
        // poses in one frame — is stated here as two different clips at the same
        // time, which also proves the clip sets are per node.
        auto swing = armrig::buildSwingClip(-90.0f);
        swing->setName("swing");
        auto still = armrig::buildSwingClip(0.0f);
        still->setName("still");
        a->addAnimation(swing); a->setAnimation(still);
        a->addAnimation(still);
        b->addAnimation(swing); b->addAnimation(still); b->setAnimation(still);
        scene->getRootNode()->addChild(a);
        scene->getRootNode()->addChild(b);

        View *v = engine->createOffscreenView("multi", 160, 96, Colour(0, 0, 1));
        Scene *s = engine->createScene("multi");
        v->setScene(s);
        s->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
        CameraDesc cam; cam.position = Vec3(0, 1, 5); v->setCamera(cam);

        SceneMirror mirror(s);
        mirror.setSource(scene);
        mirror.setLightWires(false);

        // Frame 1: both on the still clip.
        scene->updateSceneAnimation(0.0f);
        mirror.sync();
        render(engine.get());
        Image f1;
        CHECK(v->readPixels(f1), "T4: readPixels (both at bind)");

        // Frame 2: A switches to the swinging clip; B stays on the still one.
        // B must not budge — which is the whole claim: two nodes on ONE mesh
        // asset, two independent poses, one frame. On the CPU path this was
        // impossible by construction (one engine mesh per mesh asset means one
        // vertex buffer for both).
        a->setAnimation(swing);
        scene->updateSceneAnimation(0.5f);
        mirror.sync();
        render(engine.get());
        Image f2;
        CHECK(v->readPixels(f2), "T4: readPixels (A moved)");

        auto halfDiff = [&](const Image &x, const Image &y, unsigned x0, unsigned x1) {
            int n = 0;
            for (unsigned py = 0; py < x.height; ++py)
                for (unsigned px = x0; px < x1; ++px) {
                    const Colour ca = x.at(px, py), cb = y.at(px, py);
                    if (std::fabs(double(ca.r - cb.r)) > 2.0 / 255.0 ||
                        std::fabs(double(ca.b - cb.b)) > 2.0 / 255.0) ++n;
                }
            return n;
        };
        const unsigned mid = f1.width / 2;
        const int leftChanged  = halfDiff(f1, f2, 0, mid);
        const int rightChanged = halfDiff(f1, f2, mid, f1.width);
        std::printf("    A's half changed %d px, B's half changed %d px\n", leftChanged, rightChanged);
        CHECK(leftChanged > 40, "T4: posing avatar A moved avatar A");
        CHECK(rightChanged == 0, "T4: ... and left avatar B, on the SAME mesh asset, untouched");

        mirror.setSource(iris::ScenePtr());
        engine->destroyView(v); engine->destroyScene(s);
    }

    // ================= T5: the tangent =================
    // A normal-mapped strip rotated 90 degrees about Z by its bone, versus the
    // same strip whose GEOMETRY is authored already rotated. Skinning the tangent
    // makes the two light the same; keeping the bind tangent does not.
    {
        Strip strip = buildStrip(8);
        auto docMesh = buildStripDocMesh(strip.gpuData);
        Doc doc = buildDoc(docMesh);
        // Weight EVERY vertex to the tip bone so the whole strip rotates rigidly
        // and a statically rotated copy is an exact equivalent.
        for (size_t i = 0; i < strip.gpuData.vertexCount(); ++i) {
            strip.gpuData.blendIndices[i*4] = 1;
            strip.gpuData.blendWeights[i*4] = 1.0f;
            strip.gpuData.blendWeights[i*4+1] = 0.0f;
        }

        View *v = engine->createOffscreenView("tangent", 96, 96, Colour(0, 0, 0));
        Scene *s = engine->createScene("tangent");
        v->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        CameraDesc cam; cam.position = Vec3(0, 1, 4); v->setCamera(cam);
        // One light from the side, so the shading depends on the normal
        // direction the tangent frame produces.
        NodeId ln = s->createNode();
        s->setNodeTransform(ln, Vec3(3, 1, 3), Quat(), Vec3(1, 1, 1));
        LightDesc ld; ld.type = LightType::Point; ld.colour = Colour(1, 1, 1);
        ld.intensity = 30.0f; ld.range = 40.0f;
        s->setLight(ln, ld);

        // A normal map that tilts hard along the tangent (+X in tangent space).
        std::vector<unsigned char> px(4 * 4 * 4);
        for (int i = 0; i < 16; ++i) {
            px[i*4+0] = 230;   // x = +0.8
            px[i*4+1] = 128;   // y =  0
            px[i*4+2] = 205;   // z = +0.6
            px[i*4+3] = 255;
        }
        TextureId nmap = s->createTexture(4, 4, px.data(), false);
        PbrParams mp; mp.albedo = Colour(0.9f, 0.9f, 0.9f); mp.roughness = 0.35f; mp.metalness = 0.0f;
        MaterialId mat = s->createPbrMaterial(mp);
        s->setPbrTexture(mat, PbrTextureSlot::Normal, nmap);

        // (a) skinned, rotated by the bone
        MeshId skinned = s->createMesh(strip.gpuData);
        NodeId nSkinned = s->createNode();
        SkeletonDesc rig; SceneMirror::toSkeletonDesc(doc.node->getSkeleton(), rig);
        CHECK(s->attachSkinnedMesh(nSkinned, skinned, mat, rig), "T5: skinned strip attached");
        std::vector<BonePose> poses = armPoses(1.0f);   // the full -90 degree swing
        s->setBonePoses(nSkinned, poses.data(), poses.size());
        render(engine.get());
        Image gpu; v->readPixels(gpu);
        s->setNodeVisible(nSkinned, false);

        // (b) the same geometry, statically rotated the same way, unskinned.
        MeshData staticData = strip.gpuData;
        staticData.blendIndices.clear(); staticData.blendWeights.clear();
        {
            const QMatrix4x4 m = armrig::swingSkinMatrices(-90.0f, 1.0f)[1];
            for (size_t i = 0; i < staticData.vertexCount(); ++i) {
                const QVector3D p(staticData.positions[i*3], staticData.positions[i*3+1], staticData.positions[i*3+2]);
                const QVector3D n(staticData.normals[i*3], staticData.normals[i*3+1], staticData.normals[i*3+2]);
                const QVector3D tp = m.map(p);
                const QVector3D tn = m.mapVector(n).normalized();
                staticData.positions[i*3] = tp.x(); staticData.positions[i*3+1] = tp.y(); staticData.positions[i*3+2] = tp.z();
                staticData.normals[i*3] = tn.x(); staticData.normals[i*3+1] = tn.y(); staticData.normals[i*3+2] = tn.z();
            }
        }
        MeshId statik = s->createMesh(staticData);
        NodeId nStatic = s->createNode();
        s->attachMesh(nStatic, statik, mat);
        render(engine.get());
        Image ref; v->readPixels(ref);

        int diffPx = 0;
        const int worst = maxChannelDiff(gpu, ref, diffPx);
        std::printf("    T5 skinned-vs-static-equivalent: worst %d/255 over %d px\n", worst, diffPx);
        // Tangents are generated per mesh from uvs, and the static copy's uvs are
        // unchanged while its positions are rotated — so the two tangent frames
        // are the same frame expressed two ways and the shading must match.
        CHECK(worst <= 4, "T5: a normal-mapped bone rotation lights like a static rotation (tangents are skinned)");

        engine->destroyView(v); engine->destroyScene(s);
    }

    // ================= T6: the numbers =================
    {
        std::printf("\n--- T6: per-frame cost, CPU skinning vs GPU skinning ---\n");
        Strip big = buildStrip(25088);               // 50176 vertices
        auto docMesh = buildStripDocMesh(big.gpuData);
        const size_t nv = big.gpuData.vertexCount();
        std::printf("    %zu vertices per character\n", nv);

        View *v = engine->createOffscreenView("perf", 64, 64, Colour(0, 0, 0));
        Scene *s = engine->createScene("perf");
        v->setScene(s);
        s->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
        CameraDesc cam; cam.position = Vec3(0, 1, 6); v->setCamera(cam);
        PbrParams mp; mp.albedo = Colour(0.8f, 0.2f, 0.2f);
        MaterialId mat = s->createPbrMaterial(mp);

        std::vector<float> bi, bw;
        SceneMirror::toSkinData(docMesh.data(), bi, bw);
        SkeletonDesc rig;
        {
            Doc probe = buildDoc(docMesh);
            SceneMirror::toSkeletonDesc(probe.node->getSkeleton(), rig);
        }

        const int frames = 60;
        for (int count : { 1, 4, 8 }) {
            // Each character gets its own document (its own pose), as the
            // multiple-avatars case requires.
            std::vector<Doc> docs;
            std::vector<MeshId> cpuMeshes, gpuMeshes;
            std::vector<NodeId> cpuNodes, gpuNodes;
            for (int i = 0; i < count; ++i) {
                docs.push_back(buildDoc(docMesh));
                MeshId cm = s->createMesh(big.cpuData);
                NodeId cn = s->createNode();
                s->setNodeTransform(cn, Vec3(float(i) * 0.4f - 4.0f, 0, 0), Quat(), Vec3(1,1,1));
                s->attachMesh(cn, cm, mat);
                cpuMeshes.push_back(cm); cpuNodes.push_back(cn);

                MeshId gm = s->createMesh(big.gpuData);
                NodeId gn = s->createNode();
                s->setNodeTransform(gn, Vec3(float(i) * 0.4f + 0.5f, 0, 0), Quat(), Vec3(1,1,1));
                s->attachSkinnedMesh(gn, gm, mat, rig);
                gpuMeshes.push_back(gm); gpuNodes.push_back(gn);
                s->setNodeVisible(gn, false);
            }

            // --- CPU path ---
            for (auto n : gpuNodes) s->setNodeVisible(n, false);
            for (auto n : cpuNodes) s->setNodeVisible(n, true);
            std::vector<float> pos, nrm;
            QElapsedTimer timer; timer.start();
            double uploadedMb = 0;
            for (int f = 0; f < frames; ++f) {
                const float t = float(f) / float(frames);
                const QVector<QMatrix4x4> skin = armrig::swingSkinMatrices(-90.0f, t);
                for (int i = 0; i < count; ++i) {
                    SceneMirror::skinVertices(skin, big.cpuData.positions, big.cpuData.normals,
                                              bi, bw, pos, nrm);
                    s->updateMeshVertices(cpuMeshes[size_t(i)], pos, nrm);
                    uploadedMb += double(nv * 12 * sizeof(float)) / (1024.0 * 1024.0);
                }
                engine->renderOneFrame();
            }
            const double cpuMs = double(timer.nsecsElapsed()) / 1e6 / frames;
            const double cpuMb = uploadedMb / frames;

            // --- GPU path ---
            for (auto n : cpuNodes) s->setNodeVisible(n, false);
            for (auto n : gpuNodes) s->setNodeVisible(n, true);
            timer.restart();
            for (int f = 0; f < frames; ++f) {
                const float t = float(f) / float(frames);
                const std::vector<BonePose> poses = armPoses(t);
                for (int i = 0; i < count; ++i)
                    s->setBonePoses(gpuNodes[size_t(i)], poses.data(), poses.size());
                engine->renderOneFrame();
            }
            const double gpuMs = double(timer.nsecsElapsed()) / 1e6 / frames;

            std::printf("    %d character%s: CPU %7.2f ms/frame (%.2f MB uploaded/frame)   "
                        "GPU %7.2f ms/frame (0.00 MB)   speedup x%.1f\n",
                        count, count == 1 ? " " : "s", cpuMs, cpuMb, gpuMs,
                        gpuMs > 0 ? cpuMs / gpuMs : 0.0);

            for (auto n : cpuNodes) s->removeNode(n);
            for (auto n : gpuNodes) s->removeNode(n);
            for (auto m : cpuMeshes) s->destroyMesh(m);
            for (auto m : gpuMeshes) s->destroyMesh(m);
        }
        engine->destroyView(v); engine->destroyScene(s);
        CHECK(true, "T6: perf numbers printed (non-failing)");
    }

    engine.reset();
    std::printf(failures ? "FAILED: %d checks\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
