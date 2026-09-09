#pragma once
// THE MULTI-PIECE CHARACTER (AVATAR_RIG_PERF_SPEC D6, option (a)).
//
// The rig-perf program exists for characters made of SEVERAL skinned pieces on
// one skeleton — the shape every RPM/Mixamo-class character has and the shape
// the spec measured on Jennifer.fbx (5 pieces, 52/6/6/6/7 bones, union 53,
// bone orders differing across pieces, §0.4). No Mixamo file may live in the
// tree (licence), so this builds the same SHAPE synthetically, in code:
//
//     root                            bind (0,0,0)      depth 0
//       spine                              (0,1,0)      depth 1
//         neck                             (0,2,0)      depth 2
//           head                           (0,2.5,0)    depth 3
//             eyeL                         (-.1,2.6,.2) depth 4
//             eyeR                         ( .1,2.6,.2) depth 4
//         armL                             (-.5,1.8,0)  depth 2
//         armR                             ( .5,1.8,0)  depth 2
//
//   piece      bones, IN THE PIECE'S OWN ORDER            count
//   body       root, spine, neck, armL, armR                5
//   headMesh   head, neck                                   2   (reversed vs the union)
//   eyeLMesh   eyeL, head                                   2
//   eyeRMesh   head, eyeR                                   2   (reversed vs eyeLMesh)
//   hair       head, neck, root                             3   (depth-descending)
//
// Union = 8 bones; Sigma piece-local bones = 14. Today's identity blend-index
// map streams 5 x 8 = 40 bone matrices per pass for this character; the remap
// (P1a) streams 14, and sharing (P1b) turns 5 SkeletonInstances into 1. Those
// are the two numbers the bench and the gates are about, and they are only
// interesting because the pieces are SUBSETS in DIFFERENT ORDERS — a fixture
// whose pieces all carried the full rig in one order would prove nothing.
//
// WHAT ELSE IS DELIBERATE:
//  * every piece's bind matrix for a shared bone is bit-identical (that is what
//    makes one union bind pose legal, §0.4's "offsets agree, diff 0.0"), and
//    every piece node sits at identity under the character root (what makes
//    "the slave renders where the master is" the CORRECT position);
//  * the bone SCENE NODES live once, under the character root, because that is
//    where ClipExtractor looks them up (by name, from the clip host down);
//  * the mesh ASSETS are shared between characters, as an imported character's
//    are: the mirror caches meshes and materials by pointer, so four characters
//    hold five meshes and twenty Items.

#include <QVector>

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/irisglfwd.h"

namespace multipiece {

struct BoneSpec { const char *name; const char *parent; float x, y, z; };

/// The character rig — the UNION every piece is a subset of.
inline const QVector<BoneSpec> &bones()
{
    static const QVector<BoneSpec> kBones = {
        { "root", nullptr,  0.0f, 0.0f,  0.0f },
        { "spine", "root",  0.0f, 1.0f,  0.0f },
        { "neck", "spine",  0.0f, 2.0f,  0.0f },
        { "head", "neck",   0.0f, 2.5f,  0.0f },
        { "eyeL", "head",  -0.1f, 2.6f,  0.2f },
        { "eyeR", "head",   0.1f, 2.6f,  0.2f },
        { "armL", "spine", -0.5f, 1.8f,  0.0f },
        { "armR", "spine",  0.5f, 1.8f,  0.0f },
    };
    return kBones;
}

struct PieceSpec { const char *name; QVector<const char *> bones; };

/// The five pieces, each naming its bones IN ITS OWN ORDER.
inline const QVector<PieceSpec> &pieces()
{
    static const QVector<PieceSpec> kPieces = {
        { "body",     { "root", "spine", "neck", "armL", "armR" } },
        { "headMesh", { "head", "neck" } },
        { "eyeLMesh", { "eyeL", "head" } },
        { "eyeRMesh", { "head", "eyeR" } },
        { "hair",     { "head", "neck", "root" } },
    };
    return kPieces;
}

/// Sigma over the pieces of their OWN bone counts (14 for this character): what
/// the Hlms streams per pass once the blend-index map is compacted (P1a), and
/// what `pieces * union` (40) is today.
inline size_t pieceLocalBoneTotal()
{
    size_t n = 0;
    for (const PieceSpec &p : pieces()) n += size_t(p.bones.size());
    return n;
}

inline const BoneSpec *boneSpec(const char *name)
{
    for (const BoneSpec &b : bones())
        if (qstrcmp(b.name, name) == 0) return &b;
    return nullptr;
}

/// The bind mesh-space matrix of a union bone: a pure translation to its bind
/// point. Identical for every piece that carries the bone — the precondition
/// the union rig is built on.
inline iris::Mat4 bindMeshSpace(const char *name)
{
    const BoneSpec *b = boneSpec(name);
    iris::Mat4 m;
    m.setToIdentity();
    if (b) m.translate(iris::Vec3(b->x, b->y, b->z));
    return m;
}

/// The nearest ancestor of `name` that this piece also carries, or null when
/// the piece carries none — exactly what Mesh::extractSkeleton records for an
/// imported piece (the nearest BONE ancestor, pivots skipped).
inline const char *nearestPresentParent(const PieceSpec &piece, const char *name)
{
    const BoneSpec *b = boneSpec(name);
    for (const char *p = b ? b->parent : nullptr; p; p = boneSpec(p) ? boneSpec(p)->parent : nullptr)
        for (const char *own : piece.bones)
            if (qstrcmp(own, p) == 0) return p;
    return nullptr;
}

/// One piece's mesh: a quad per bone, weighted 100% to that bone's PIECE-LOCAL
/// index, plus the piece's own subset skeleton.
inline iris::MeshPtr buildPieceMesh(const PieceSpec &piece)
{
    const int n = piece.bones.size();
    QVector<float> positions, normals, boneIdx, boneW;
    QVector<unsigned> indices;
    const float hw = 0.12f;
    for (int b = 0; b < n; ++b) {
        const BoneSpec *bs = boneSpec(piece.bones[b]);
        const float x = bs ? bs->x : 0.0f, y = bs ? bs->y : 0.0f, z = bs ? bs->z : 0.0f;
        const unsigned base = unsigned(positions.size() / 3);
        const float quad[4][3] = { { x - hw, y, z }, { x + hw, y, z },
                                   { x - hw, y + 0.4f, z }, { x + hw, y + 0.4f, z } };
        for (const auto &v : quad) {
            positions << v[0] << v[1] << v[2];
            normals << 0.0f << 0.0f << 1.0f;
            boneIdx << float(b) << 0.0f << 0.0f << 0.0f;
            boneW << 1.0f << 0.0f << 0.0f << 0.0f;
        }
        indices << base << base + 1 << base + 3 << base << base + 3 << base + 2;
    }

    auto mesh = iris::Mesh::create();
    auto addBuf = [&mesh](iris::VertexAttribUsage usage, const QVector<float> &data, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(data.constData()), unsigned(data.size() * sizeof(float)));
        mesh->addVertexBuffer(vb);
    };
    addBuf(iris::VertexAttribUsage::Position, positions, 3);
    addBuf(iris::VertexAttribUsage::Normal, normals, 3);
    addBuf(iris::VertexAttribUsage::BoneIndices, boneIdx, 4);
    addBuf(iris::VertexAttribUsage::BoneWeights, boneW, 4);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(indices.constData()), unsigned(indices.size() * sizeof(unsigned)));
    mesh->setIndexBuffer(ib);
    mesh->setVertexCount(positions.size() / 3);

    auto skel = iris::Skeleton::create();
    for (const char *bn : piece.bones) {
        auto bone = iris::Bone::create(QString::fromLatin1(bn));
        bone->meshSpacePoseMatrix = bindMeshSpace(bn);
        bone->inverseMeshSpacePoseMatrix = bone->meshSpacePoseMatrix.inverted();
        skel->addBone(bone);
    }
    // The subset hierarchy: nearest ancestor the piece also carries.
    for (const char *bn : piece.bones) {
        const char *parent = nearestPresentParent(piece, bn);
        if (!parent) continue;
        skel->getBone(QString::fromLatin1(parent))->addChild(skel->getBone(QString::fromLatin1(bn)));
    }
    mesh->setSkeleton(skel);
    return mesh;
}

/// The five piece meshes, built ONCE and shared by every character (the shape
/// an imported character duplicated four times has).
inline const QVector<iris::MeshPtr> &sharedPieceMeshes()
{
    static QVector<iris::MeshPtr> meshes;
    if (meshes.isEmpty())
        for (const PieceSpec &p : pieces()) meshes.append(buildPieceMesh(p));
    return meshes;
}

/// A character: the fragment root (the clip host), the bone scene nodes, and
/// the five skinned MeshNodes.
struct Character {
    iris::SceneNodePtr root;
    QVector<iris::MeshNodePtr> pieces;
    QHash<QString, iris::SceneNodePtr> boneNodes;
};

inline Character buildCharacter(const QString &name)
{
    Character c;
    c.root = iris::SceneNode::create();
    c.root->setName(name);

    // The bone scene nodes: ONE tree per character, under the root, named after
    // the union bones — ClipExtractor resolves a clip's channels through these.
    for (const BoneSpec &b : bones()) {
        auto node = iris::SceneNode::create();
        node->setName(QString::fromLatin1(b.name));
        const BoneSpec *parent = b.parent ? boneSpec(b.parent) : nullptr;
        node->setLocalPos(iris::Vec3(b.x - (parent ? parent->x : 0.0f),
                                     b.y - (parent ? parent->y : 0.0f),
                                     b.z - (parent ? parent->z : 0.0f)));
        c.boneNodes.insert(node->getName(), node);
    }
    for (const BoneSpec &b : bones()) {
        const iris::SceneNodePtr &node = c.boneNodes[QString::fromLatin1(b.name)];
        if (b.parent) c.boneNodes[QString::fromLatin1(b.parent)]->addChild(node, false);
        else c.root->addChild(node, false);
    }

    const QVector<iris::MeshPtr> &meshes = sharedPieceMeshes();
    for (int i = 0; i < pieces().size(); ++i) {
        auto mn = iris::MeshNode::create();
        mn->setName(QString::fromLatin1(pieces()[i].name));
        mn->setMesh(meshes[i]);                     // clones the rig template per node
        auto mat = iris::DefaultMaterial::create();
        mat->setDiffuseColor(QColor(220, 60, 60));
        mn->setMaterial(mat);
        c.root->addChild(mn, false);                // identity local: every piece at the root
        c.pieces.append(mn);
    }
    return c;
}

/// One clip for the whole character: `spine` swings about Z, `head` about X, so
/// EVERY piece is posed (the eyes and the hair ride the head, the arms ride the
/// spine) and a follower's matrices are worth comparing against a master's.
inline iris::AnimationPtr buildCharacterClip(float length = 1.0f)
{
    auto skelAnim = iris::SkeletalAnimation::create();
    auto swing = [&](const char *bone, const iris::Vec3 &pos, float x, float y, float z, float deg) {
        auto ba = new iris::BoneAnimation();
        ba->posKeys->addKey(pos, 0.0f);
        ba->posKeys->addKey(pos, length);
        ba->rotKeys->addKey(iris::Quat(), 0.0f);
        ba->rotKeys->addKey(iris::Quat::fromAxisAndAngle(x, y, z, deg), length);
        ba->scaleKeys->addKey(iris::Vec3(1, 1, 1), 0.0f);
        ba->scaleKeys->addKey(iris::Vec3(1, 1, 1), length);
        skelAnim->addBoneAnimation(QString::fromLatin1(bone), ba);
    };
    swing("spine", iris::Vec3(0, 1, 0), 0, 0, 1, 25.0f);
    swing("head", iris::Vec3(0, 0.5f, 0), 1, 0, 0, 20.0f);
    return iris::Animation::createFromSkeletalAnimation(skelAnim);
}

}  // namespace multipiece
