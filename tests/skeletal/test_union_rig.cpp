// THE CHARACTER RIG: union + per-piece remap (AVATAR_RIG_PERF_SPEC P1a).
//
// An imported character is several skinned pieces, and each piece's rig is the
// bones THAT PIECE weights, in that piece's order (Mesh::extractSkeleton). Five
// pieces are therefore five rigs, five SkeletonDefs and five SkeletonInstances —
// and Ogre refuses to let two Items share a SkeletonInstance unless their meshes
// name the SAME skeleton (OgreItem.cpp:249-254), so sharing is not merely
// unhelpful on per-piece rigs, it is illegal. The prerequisite is ONE rig per
// character (the union) plus a per-piece blend-index remap.
//
// This suite is the union's proof, and it needs NO DISPLAY AND NO GPU: the maths
// is document-side and the engine half (attach, refuse, count) runs on the NULL
// render system. It never renders — the pose half of the claim (a follower's
// matrices are the master's permuted) belongs to the sharing suite, which does.
//
// WHAT IS PROVED HERE
//   A  the union of five subsets is ONE rig of the eight bones, hierarchy
//      rebuilt from what the pieces together know, in a CANONICAL order;
//   B  its id does not depend on the order the pieces were visited in — the rig
//      id is a structure hash and the engine's rig cache is keyed on it, so an
//      order-dependent union would hash one character as two rigs;
//   C  the remap round-trips: union[map[i]] IS the piece's bone i;
//   D  a piece that disagrees about a shared bone's BIND POSE is excluded and
//      keeps its own rig, rather than being averaged into a wrong one;
//   E  a character with ONE piece gets no union at all — the negative gate: it
//      binds its own rig with no map, exactly as before this program;
//   F  engine-side, a piece attached with a map streams ITS OWN bones per pass
//      (streamedBoneCount), while an unmapped attach still streams the whole rig;
//   G  the refusals: a map naming a bone the rig has not, and two nodes asking
//      one mesh asset for two different maps (the map lives on the SubMesh).

#include <QGuiApplication>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "../support/documentgraph.h"
#include "multipiecerig.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QVector<iris::SkeletonPtr> pieceSkeletons()
{
    QVector<iris::SkeletonPtr> out;
    for (const iris::MeshPtr &m : multipiece::sharedPieceMeshes()) out.append(m->getSkeleton());
    return out;
}

/// A piece's mesh as the engine's MeshData, blend indices included — the same
/// two static mirror helpers SceneMirror::meshFor uses.
static MeshData pieceMeshData(const iris::MeshPtr &mesh)
{
    MeshData d;
    SceneMirror::toMeshData(mesh.data(), d);
    std::vector<float> bi, bw;
    SceneMirror::toSkinData(mesh.data(), bi, bw);
    d.blendIndices.resize(bi.size());
    for (size_t i = 0; i < bi.size(); ++i) d.blendIndices[i] = (unsigned char)(int)bi[i];
    d.blendWeights = bw;
    return d;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // A document node IS an engine node (SCENEGRAPH_SPEC D2), and the engine
    // half below runs on the same headless engine: NULL render system, no
    // display, no driver.
    enginetest::DocumentGraph dg("union-rig-ogre.log");
    if (!dg.require()) return 1;

    const QVector<iris::SkeletonPtr> pieces = pieceSkeletons();
    CHECK(pieces.size() == 5, "five piece rigs");

    // ---- A. the union ---------------------------------------------------
    iris::SkeletonPtr rig;
    QVector<int> excluded;
    QString why;
    CHECK(SceneMirror::buildUnionSkeleton(pieces, rig, &excluded, &why),
          "the five subsets union into one rig");
    if (rig.isNull()) { std::printf("    %s\n", qUtf8Printable(why)); return 1; }
    CHECK(excluded.isEmpty(), "no piece was excluded");
    CHECK(rig->bones.size() == multipiece::bones().size(), "the union has every bone, once");

    // CANONICAL ORDER: depth, then name. Written out rather than derived, so a
    // change of ordering rule fails here and not as a mystery rig-id change.
    const char *kOrder[] = { "root", "spine", "armL", "armR", "neck", "head", "eyeL", "eyeR" };
    bool ordered = rig->bones.size() == 8;
    for (int i = 0; ordered && i < 8; ++i) ordered = rig->bones[i]->name == QLatin1String(kOrder[i]);
    CHECK(ordered, "bones come out in canonical (depth, name) order");

    // The HIERARCHY, rebuilt from subsets that each knew only part of it: the
    // hair piece carries {head, neck, root} and the body {root..armR}; only
    // together do they say head's parent is neck and neck's is spine.
    bool parents = true;
    for (const multipiece::BoneSpec &b : multipiece::bones()) {
        const iris::BonePtr bone = rig->getBone(QString::fromLatin1(b.name));
        if (bone.isNull()) { parents = false; break; }
        const QString want = b.parent ? QString::fromLatin1(b.parent) : QString();
        const QString got = bone->parentBone.isNull() ? QString() : bone->parent()->name;
        if (want != got) { parents = false; std::printf("    %s: parent '%s', wanted '%s'\n",
                                                        b.name, qUtf8Printable(got),
                                                        qUtf8Printable(want)); break; }
    }
    CHECK(parents, "every union bone's parent is the DEEPEST ancestor any piece named");

    // ---- B. the id does not depend on visit order ------------------------
    SkeletonDesc desc;
    CHECK(SceneMirror::toSkeletonDesc(rig, desc), "the union translates to a SkeletonDesc");
    {
        QVector<iris::SkeletonPtr> shuffled;
        for (int i = pieces.size() - 1; i >= 0; --i) shuffled.append(pieces[i]);
        iris::SkeletonPtr other;
        SkeletonDesc otherDesc;
        CHECK(SceneMirror::buildUnionSkeleton(shuffled, other, nullptr, nullptr) &&
                  SceneMirror::toSkeletonDesc(other, otherDesc),
              "the reversed piece order also unions");
        CHECK(otherDesc.id == desc.id, "...to the SAME rig id (the cache key is a structure hash)");
    }

    // ---- C. the remap round-trips ---------------------------------------
    size_t mapped = 0;
    for (int p = 0; p < pieces.size(); ++p) {
        QVector<unsigned short> map;
        if (!SceneMirror::rigRemap(pieces[p], rig, map)) {
            std::printf("FAIL: piece %d has no remap\n", p); ++failures; continue;
        }
        bool ok = map.size() == pieces[p]->bones.size();
        for (int i = 0; ok && i < map.size(); ++i)
            ok = map[i] < rig->bones.size() &&
                 rig->bones[map[i]]->name == pieces[p]->bones[i]->name;
        CHECK(ok, "the piece's blend indices map onto the union's bones");
        mapped += size_t(map.size());
    }
    CHECK(mapped == multipiece::pieceLocalBoneTotal(),
          "the maps together are Sigma piece-local bones (14), not pieces x union (40)");

    // ---- D. a piece rigged in another space is EXCLUDED ------------------
    {
        QVector<iris::SkeletonPtr> odd = pieces;
        // `head` is carried by the body-less pieces AND by hair (3 bones), and
        // hair is merged first (biggest-piece-first), so the rogue is the one
        // that disagrees — not the majority.
        iris::SkeletonPtr rogue = pieces[1]->clone();
        rogue->bones[0]->meshSpacePoseMatrix.translate(iris::Vec3(0, 3, 0));   // head, moved
        odd[1] = rogue;
        iris::SkeletonPtr unionOfRest;
        QVector<int> ex;
        CHECK(SceneMirror::buildUnionSkeleton(odd, unionOfRest, &ex, nullptr),
              "the remaining pieces still union");
        CHECK(ex.contains(1), "the piece that disagrees about a bind pose is excluded");
        // ...and the union it was excluded from is UNPOLLUTED: `head` still
        // binds where the pieces that agree put it. (The mirror is what refuses
        // the excluded piece a map; `rigRemap` is a name lookup and would
        // happily produce one, which is why the union's own bind is the thing
        // worth asserting here.)
        bool clean = !unionOfRest.isNull() && !unionOfRest->getBone("head").isNull();
        if (clean) {
            const iris::Mat4 got = unionOfRest->getBone("head")->meshSpacePoseMatrix;
            const iris::Mat4 want = multipiece::bindMeshSpace("head");
            for (int r = 0; r < 4 && clean; ++r)
                for (int c = 0; c < 4 && clean; ++c)
                    clean = std::fabs(double(got(r, c)) - double(want(r, c))) < 1e-5;
        }
        CHECK(clean, "the union keeps the agreed bind pose, not the rogue's");
    }

    // ---- E. one piece = no union (the negative gate) ---------------------
    {
        QVector<iris::SkeletonPtr> lone;
        lone.append(pieces[0]);
        iris::SkeletonPtr none;
        CHECK(!SceneMirror::buildUnionSkeleton(lone, none, nullptr, nullptr),
              "a single-piece character gets no union at all");
    }

    // ---- F/G. the engine half -------------------------------------------
    Scene *scene = dg.engine()->createScene("union");
    CHECK(scene != nullptr, "engine scene");
    if (!scene) return 1;
    PbrParams pbr;
    pbr.albedo = Colour(0.8f, 0.2f, 0.2f);
    const MaterialId mat = scene->createPbrMaterial(pbr);

    const MeshId bodyMesh = scene->createMesh(pieceMeshData(multipiece::sharedPieceMeshes()[0]));
    const MeshId eyeMesh = scene->createMesh(pieceMeshData(multipiece::sharedPieceMeshes()[2]));
    CHECK(bodyMesh && eyeMesh, "two piece meshes uploaded");

    QVector<unsigned short> bodyMap, eyeMap;
    SceneMirror::rigRemap(pieces[0], rig, bodyMap);
    SceneMirror::rigRemap(pieces[2], rig, eyeMap);

    const NodeId body = scene->createNode();
    const NodeId eye = scene->createNode();
    CHECK(scene->attachSkinnedMesh(body, bodyMesh, mat, desc, bodyMap.constData(),
                                   size_t(bodyMap.size())),
          "the body piece binds the CHARACTER rig with its own map");
    CHECK(scene->attachSkinnedMesh(eye, eyeMesh, mat, desc, eyeMap.constData(),
                                   size_t(eyeMap.size())),
          "the eye piece binds the SAME rig — the precondition for sharing");
    CHECK(scene->boneNames(body).size() == rig->bones.size() &&
              scene->boneNames(eye).size() == rig->bones.size(),
          "both pieces speak the union's bones");
    CHECK(scene->streamedBoneCount(body) == 5, "the body streams its 5 bones, not the union's 8");
    CHECK(scene->streamedBoneCount(eye) == 2, "the eye streams its 2 bones, not the union's 8");
    {
        const RigStats rs = scene->rigStats();
        CHECK(rs.rigged == 2 && rs.instances == 2 && rs.shared == 0,
              "two pieces, two instances, nothing shared yet");
        CHECK(rs.streamedBones == 7, "streamed bones is the sum of the maps (5 + 2)");
    }

    // The IDENTITY path, byte for byte what every rigged node did before this
    // program: no map, and the whole rig streams.
    {
        SkeletonDesc own;
        CHECK(SceneMirror::toSkeletonDesc(pieces[2], own), "the piece's own rig translates");
        const MeshId m = scene->createMesh(pieceMeshData(multipiece::sharedPieceMeshes()[2]));
        const NodeId n = scene->createNode();
        CHECK(scene->attachSkinnedMesh(n, m, mat, own), "a lone piece attaches with no map");
        CHECK(scene->streamedBoneCount(n) == own.bones.size(),
              "...and streams its whole rig, exactly as before (identity map)");
    }

    // ---- G. refusals -----------------------------------------------------
    {
        QVector<unsigned short> wild = eyeMap;
        wild[0] = (unsigned short)(rig->bones.size() + 3);
        const MeshId m = scene->createMesh(pieceMeshData(multipiece::sharedPieceMeshes()[2]));
        const NodeId n = scene->createNode();
        CHECK(!scene->attachSkinnedMesh(n, m, mat, desc, wild.constData(), size_t(wild.size())),
              "a map naming a bone the rig has not is refused");
        // ...and the SAME mesh cannot carry two different maps: the map is a
        // SubMesh member, so rewriting it would re-target the other node's
        // weights with nothing anywhere saying so.
        const NodeId n2 = scene->createNode();
        CHECK(scene->attachSkinnedMesh(n2, m, mat, desc, eyeMap.constData(), size_t(eyeMap.size())),
              "a fresh mesh takes the eye map");
        QVector<unsigned short> other = eyeMap;
        std::swap(other[0], other[1]);
        const NodeId n3 = scene->createNode();
        CHECK(!scene->attachSkinnedMesh(n3, m, mat, desc, other.constData(), size_t(other.size())),
              "a second map on the same mesh asset is refused");
    }

    dg.engine()->destroyScene(scene);

    // ---- H. the MIRROR puts a whole character on one rig -----------------
    //
    // The integration, and the P1b comparison point: five pieces, five Items,
    // ONE rig, five per-piece maps. What is still five here is the number of
    // SkeletonInstances and the number of clip pushes — those are what sharing
    // removes, and the numbers are asserted so that the sharing phase's
    // improvement is against a recorded fact and not a memory.
    {
        auto doc = iris::Scene::create();
        multipiece::Character hero = multipiece::buildCharacter("hero");
        auto clip = multipiece::buildCharacterClip();
        clip->setName("Walk");
        clip->setLooping(true);
        hero.root->addAnimation(clip);
        hero.root->setAnimation(clip);
        doc->getRootNode()->addChild(hero.root, false);

        Scene *ms = dg.engine()->createScene("mirror");
        SceneMirror mirror(ms);
        mirror.setSource(doc);
        doc->updateSceneAnimation(0.0f);
        mirror.sync();

        size_t rigged = 0, streamed = 0;
        std::vector<std::string> firstNames;
        bool oneRig = true;
        for (const iris::MeshNodePtr &piece : hero.pieces) {
            const NodeId n = mirror.engineNode(piece.data());
            if (!n || !ms->hasSkeleton(n)) continue;
            ++rigged;
            streamed += ms->streamedBoneCount(n);
            const std::vector<std::string> names = ms->boneNames(n);
            if (firstNames.empty()) firstNames = names;
            else oneRig = oneRig && names == firstNames;
        }
        CHECK(rigged == size_t(hero.pieces.size()), "the mirror rigs every piece");
        CHECK(oneRig && firstNames.size() == multipiece::bones().size(),
              "every piece is on the SAME rig — the character union");
        CHECK(streamed == multipiece::pieceLocalBoneTotal(),
              "the character streams Sigma piece-local bones (14), not pieces x union (40)");
        const RigStats rs = ms->rigStats();
        CHECK(rs.rigged == 5 && rs.streamedBones == multipiece::pieceLocalBoneTotal(),
              "rigStats agrees");
        std::printf("    instances=%zu shared=%zu streamed=%zu\n", rs.instances, rs.shared,
                    rs.streamedBones);
        CHECK(rs.instances == 1 && rs.shared == 4,
              "...on ONE SkeletonInstance: four pieces follow the body (P1b)");

        // The per-frame clip push, steady state: one per skinned NODE today.
        doc->updateSceneAnimation(0.1f);
        mirror.sync();
        const quint64 before = mirror.clipStatePushes();
        doc->updateSceneAnimation(0.2f);
        mirror.sync();
        const quint64 pushes = mirror.clipStatePushes() - before;
        std::printf("    clip pushes this frame: %llu\n", (unsigned long long)pushes);
        CHECK(pushes == 1,
              "ONE clip push per frame for the whole character (was one per piece)");

        // ---- a piece the USER MOVES un-shares itself (§3.4, §5.3) --------
        //
        // Ogre's shared bones carry the MASTER's node transform, so a piece that
        // is no longer where the master is may not share — it goes back to its
        // own instance, correct and slower, and comes back when it does.
        {
            const iris::MeshNodePtr hair = hero.pieces.last();
            hair->setLocalPos(iris::Vec3(0.75f, 0, 0));
            doc->updateSceneAnimation(0.3f);
            mirror.sync();
            const NodeId hairNode = mirror.engineNode(hair.data());
            CHECK(!ms->sharesSkeleton(hairNode), "a moved piece stops sharing");
            const RigStats moved = ms->rigStats();
            CHECK(moved.instances == 2 && moved.shared == 3,
                  "...onto an instance of its own; the other three still share");
            // Its clips come back with it: a piece on its own instance that was
            // never given clips would freeze at bind pose.
            const quint64 before2 = mirror.clipStatePushes();
            doc->updateSceneAnimation(0.4f);
            mirror.sync();
            CHECK(mirror.clipStatePushes() - before2 == 2,
                  "the un-shared piece is driven by its own clip push");

            hair->setLocalPos(iris::Vec3(0, 0, 0));
            doc->updateSceneAnimation(0.5f);
            mirror.sync();
            CHECK(ms->sharesSkeleton(hairNode), "moving it back re-shares it");
            CHECK(ms->rigStats().instances == 1, "one instance again");
        }

        mirror.setSource(nullptr);
        dg.engine()->destroyScene(ms);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall good\n", failures);
    return failures ? 1 : 0;
}
