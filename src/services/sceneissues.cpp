/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/sceneissues.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QHash>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <functional>

#include "irisgl/document/assets/livetextures.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/material.h"
#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "bridge/enginehost.h"
#include "jahshaka/engine/Engine.h"

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

/// WHAT THIS MACHINE CAN DO ABOUT RAYS: 1 yes, 0 no, -1 we do not know yet.
///
/// The document half of the ray-tracing row is the project's business; this is
/// the other half, and it is the RENDERER's answer — the device either
/// advertises Vulkan ray queries or it does not, and nothing in the editor can
/// move that. Asked at scan time rather than cached, because the answer only
/// becomes meaningful once a Vulkan device exists (Ogre creates it with the
/// first render target, not with Root) and this scanner runs on a 1 Hz timer
/// from before that.
///
/// UNKNOWN IS THE SAFE ANSWER and it is why this is a tri-state: a headless
/// process, a document-only session and the moments before the device exists
/// must raise NOTHING. An issue that says "this machine has no ray tracing" on
/// a machine that has it would be worse than saying nothing at all.
int machineRayTracing()
{
    const auto engine = EngineHost::instance().engine();
    if (!engine) return -1;
    // No capabilities object = no device yet (OgreLogBridge.cpp deviceInfo).
    if (engine->deviceInfo().deviceName.empty()) return -1;
    // The process latch (--no-ray-query / JAHSHAKA_NO_RAY_QUERY) is a genuine
    // "this run has no ray tracing": fork d014b064f (was 0038) keeps the extensions off the
    // device entirely, so this run IS a machine without the hardware, and the
    // row's promise to say so applies to it.
    if (!engine->rayTracing()) return 0;
    return engine->rayQueryAvailable() ? 1 : 0;
}

/// A texture slot's name as the material panel's rows say it ("u_baseColorMap"
/// is "Base Color"); an unknown slot keeps its own name without the prefix.
QString textureSlotName(const QString &slot)
{
    static const QHash<QString, QString> kNames = {
        { QStringLiteral("u_baseColorMap"), QStringLiteral("Base Color") },
        { QStringLiteral("u_normalMap"), QStringLiteral("Normal") },
        { QStringLiteral("u_metallicMap"), QStringLiteral("Metallic") },
        { QStringLiteral("u_roughnessMap"), QStringLiteral("Roughness") },
        { QStringLiteral("u_emissiveMap"), QStringLiteral("Emissive") },
        { QStringLiteral("u_detail0Map"), QStringLiteral("Detail 1") },
        { QStringLiteral("u_detail1Map"), QStringLiteral("Detail 2") },
        { QStringLiteral("u_detail0NormalMap"), QStringLiteral("Detail 1 Normal") },
        { QStringLiteral("u_detail1NormalMap"), QStringLiteral("Detail 2 Normal") },
        { QStringLiteral("u_detailWeightMap"), QStringLiteral("Detail Weight") },
        { QStringLiteral("u_reflectionMap"), QStringLiteral("Reflection") },
    };
    const auto it = kNames.constFind(slot);
    if (it != kNames.constEnd()) return it.value();
    return slot.startsWith(QLatin1String("u_")) ? slot.mid(2) : slot;
}

/// Does a sphere touch an axis-aligned box?
bool sphereTouchesBox(const iris::Vec3 &c, float r, const iris::Vec3 &mn, const iris::Vec3 &mx)
{
    const float dx = qMax(0.0f, qMax(mn.x() - c.x(), c.x() - mx.x()));
    const float dy = qMax(0.0f, qMax(mn.y() - c.y(), c.y() - mx.y()));
    const float dz = qMax(0.0f, qMax(mn.z() - c.z(), c.z() - mx.z()));
    return dx * dx + dy * dy + dz * dz <= r * r;
}

bool boxContains(const iris::Vec3 &p, const iris::Vec3 &mn, const iris::Vec3 &mx)
{
    return p.x() >= mn.x() && p.x() <= mx.x() && p.y() >= mn.y() && p.y() <= mx.y() &&
           p.z() >= mn.z() && p.z() <= mx.z();
}

}   // namespace

QVariantMap SceneIssue::toMap() const
{
    return QVariantMap{ { QStringLiteral("id"), id },
                        { QStringLiteral("kind"), kind },
                        { QStringLiteral("node"), node },
                        { QStringLiteral("nodeName"), nodeName },
                        { QStringLiteral("message"), message },
                        { QStringLiteral("action"), action } };
}

SceneIssues &SceneIssues::instance()
{
    static SceneIssues s;
    return s;
}

int SceneIssues::indexOf(const QString &id) const
{
    for (int i = 0; i < mIssues.size(); ++i)
        if (mIssues[i].id == id) return i;
    return -1;
}

QString SceneIssues::raise(const SceneIssue &issue, bool *raised)
{
    SceneIssue copy = issue;
    if (copy.id.isEmpty())
        copy.id = copy.node.isEmpty() ? copy.kind : (copy.kind + QLatin1Char(':') + copy.node);
    // ALREADY LIVE = NOTHING HAPPENS. Not a re-notify, not a move to the front:
    // a scanner running every second must be unable to nag.
    if (indexOf(copy.id) >= 0) {
        if (raised) *raised = false;
        return copy.id;
    }
    copy.raisedMs = nowMs();
    mIssues.append(copy);
    if (raised) *raised = true;
    emit changed();
    return copy.id;
}

bool SceneIssues::clear(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0) return false;
    mIssues.removeAt(i);
    emit changed();
    return true;
}

bool SceneIssues::update(const QString &id, const QString &message)
{
    const int i = indexOf(id);
    if (i < 0) return false;
    if (mIssues[i].message == message) return true;
    mIssues[i].message = message;
    emit changed();
    return true;
}

// ---------------------------------------------------------------------------
// texture.missing's VIEW OF THE DISK
// ---------------------------------------------------------------------------
// The scan runs at 1 Hz on the UI thread, and a stat of a path on a stale
// network mount blocks for the mount's timeout — so a path is asked of the
// filesystem when it is NEW to this cache, and after that only when its
// directory CHANGES (a QFileSystemWatcher on every distinct directory that
// holds a texture: a file deleted or put back is a directory change, so the
// next scan re-asks — raise and clear stay within ONE scan of the event).
//
// A SLOW RE-CHECK every kTextureRecheckScans passes (60 = one minute at the
// editor's 1 Hz) re-asks the disk about every cached path ON A WORKER
// (startTextureRestat): the backstop for what a watcher cannot see — a network
// filesystem whose remote changes produce no local event, a directory that did
// not exist to be watched (its file then shows up within a minute), and a
// watcher the system refused (inotify limits). The cache is no longer shed (a
// path no material names any more costs one stat a minute, on the worker).
static constexpr int kTextureRecheckScans = 60;

bool SceneIssues::textureExists(const QString &path)
{
    const auto known = mTextureExists.constFind(path);
    if (known != mTextureExists.constEnd()) return known.value();
    const QFileInfo info(path);
    const bool exists = info.exists();
    mTextureExists.insert(path, exists);
    const QString dir = info.absolutePath();
    if (!mTextureDirs) {
        mTextureDirs = new QFileSystemWatcher(this);
        connect(mTextureDirs, &QFileSystemWatcher::directoryChanged, this,
                [this](const QString &changed) { forgetTextureDir(changed); });
    }
    if (!mTextureDirs->directories().contains(dir) && QFileInfo(dir).isDir())
        mTextureDirs->addPath(dir);
    return exists;
}

void SceneIssues::forgetTextureDir(const QString &dir)
{
    for (auto it = mTextureExists.begin(); it != mTextureExists.end();) {
        if (QFileInfo(it.key()).absolutePath() == dir) {
            mFlippedPaths.insert(it.key());   // the next pass re-checks its users
            it = mTextureExists.erase(it);
        } else {
            ++it;
        }
    }
}

// THE MINUTE'S RE-CHECK, WITHOUT THE WALK AND OFF THE UI THREAD. It used to
// forget the whole cache, which made the next pass walk every mesh to ask
// again; the answers that can have changed are exactly the paths already
// cached, so a worker re-asks THOSE (the same disk traffic as before — and a
// network mount's stall is the worker's, never the editor's) and hands back
// the ones that flipped. Their users are re-checked by the next pass. A
// directory the watcher does not hold (it did not exist, or the system refused
// it) is offered to the watcher again on the way.
void SceneIssues::startTextureRestat()
{
    if (mRestatRunning || mTextureExists.isEmpty()) return;
    mRestatRunning = true;
    const QHash<QString, bool> asked = mTextureExists;
    QSet<QString> watched;
    if (mTextureDirs) {
        const QStringList dirs = mTextureDirs->directories();
        watched = QSet<QString>(dirs.begin(), dirs.end());
    }
    using Answer = QPair<QHash<QString, bool>, QStringList>;   // flipped paths, dirs to watch
    auto *watcher = new QFutureWatcher<Answer>(this);
    connect(watcher, &QFutureWatcher<Answer>::finished, this, [this, watcher]() {
        const Answer a = watcher->result();
        watcher->deleteLater();
        mRestatRunning = false;
        for (auto it = a.first.constBegin(); it != a.first.constEnd(); ++it) {
            const auto known = mTextureExists.find(it.key());
            if (known == mTextureExists.end()) continue;   // forgotten meanwhile
            known.value() = it.value();
            mFlippedPaths.insert(it.key());
        }
        if (mTextureDirs)
            for (const QString &dir : a.second) mTextureDirs->addPath(dir);
    });
    watcher->setFuture(QtConcurrent::run([asked, watched]() {
        Answer a;
        QSet<QString> offered;
        for (auto it = asked.constBegin(); it != asked.constEnd(); ++it) {
            const QFileInfo info(it.key());
            const bool exists = info.exists();
            if (exists != it.value()) a.first.insert(it.key(), exists);
            const QString dir = info.absolutePath();
            if (!watched.contains(dir) && !offered.contains(dir) && QFileInfo(dir).isDir()) {
                offered.insert(dir);
                a.second << dir;
            }
        }
        return a;
    }));
}

void SceneIssues::forgetWalks()
{
    mChecks.clear();
    mCheckedScene.clear();
    mLeakLines.clear();
    mLeakDirty = true;
}

int SceneIssues::clearKind(const QString &kind)
{
    int n = 0;
    for (int i = mIssues.size() - 1; i >= 0; --i)
        if (mIssues[i].kind == kind) { mIssues.removeAt(i); ++n; }
    if (n) emit changed();
    return n;
}

void SceneIssues::raiseMissingModels(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QString file = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
        SceneIssue issue;
        issue.id = QStringLiteral("model.missing:") + path;
        issue.kind = QStringLiteral("model.missing");
        issue.nodeName = file;
        issue.message = tr("The model '%1' is missing from the scene: the asset store holds no "
                           "current mesh bake for it.").arg(file);
        issue.action = tr("Re-import the model (or the project archive it came in); the log names "
                          "the file it looked for.");
        raise(issue);
    }
}

void SceneIssues::raiseMissingClips(const QVector<MissingClipRef> &clips)
{
    for (const MissingClipRef &clip : clips) {
        const QString asset = clip.assetName.isEmpty()
                                  ? (clip.assetGuid.isEmpty() ? tr("no asset") : clip.assetGuid)
                                  : QFileInfo(clip.assetName).fileName();
        SceneIssue issue;
        issue.id = QStringLiteral("clip.missing:%1|%2|%3")
                       .arg(clip.nodeName, clip.assetGuid, clip.clipName);
        issue.kind = QStringLiteral("clip.missing");
        issue.nodeName = clip.nodeName;
        issue.message = tr("The animation clip '%1' on '%2' (from %3) could not be loaded: %4.")
                            .arg(clip.clipName, clip.nodeName, asset, clip.why);
        issue.action = tr("The node stands in its bind pose. Load the clip again (Avatar module or "
                          "avatar.loadClip), or re-import %1.").arg(asset);
        raise(issue);
    }
}

void SceneIssues::raiseCascadeSetClamped(int dropped)
{
    if (dropped <= 0) return;
    SceneIssue issue;
    issue.id = QStringLiteral("gi.cascades.clamped");
    issue.kind = QStringLiteral("gi.cascades.clamped");
    issue.message = tr("The scene pinned %n cascade row(s) more than the renderer holds (%1); "
                       "the rows past it were dropped.", nullptr, dropped)
                        .arg(jahshaka::engine::kGiTierMaxCascades);
    issue.action = tr("Check the scene's cascade table (world.gi({cascadeSet})), or hand it back to "
                      "the tier (world.gi({cascadeSet: []})); saving keeps the clamped table.");
    raise(issue);
}

void SceneIssues::reset()
{
    forgetWalks();
    if (mIssues.isEmpty()) return;
    mIssues.clear();
    emit changed();
}

// THE ORDER IS PART OF THE CONTRACT (owner, 2026-09-13: every error listed line
// by line). Sorted on READ — by kind, then by the object, then by id — so that
// a second issue appearing cannot reshuffle the line the user is reading, and
// so that the bar, `editor.issues()` and `editor.checkScene().list` are the
// same list in the same order whatever order the scanner happened to find them
// in. Raise order is an implementation detail; nothing may depend on it.
QVector<SceneIssue> SceneIssues::issues() const
{
    QVector<SceneIssue> out = mIssues;
    std::stable_sort(out.begin(), out.end(), [](const SceneIssue &a, const SceneIssue &b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.node != b.node) return a.node < b.node;
        return a.id < b.id;
    });
    return out;
}

QVariantList SceneIssues::toVariant() const
{
    QVariantList out;
    for (const auto &i : issues()) out.append(i.toMap());
    return out;
}

int SceneIssues::count() const { return mIssues.size(); }

// ONE NODE'S ANSWERS. A blocker's box is its OWN mesh's (the eight corners of
// the mesh's bounds through the node's world matrix): a child is a blocker of
// its own when it casts. The texture line is the same existence test the
// mirror makes (SceneMirror::textureFor: a Qt resource and a live texture have
// no file and are not ours to judge), over every texture slot of the node's
// material; ONE line per node, naming each slot and its file, so the user fixes
// the object, not a list of paths. The disk is asked through textureExists:
// once per new path, again only when its directory changes or the minute's
// re-stat reports it flipped.
void SceneIssues::checkNode(iris::SceneNode *node)
{
    ++mStats.nodesVisited;
    ++mStats.visitedTotal;
    mLeakDirty = true;   // a box, a lamp or a flag may have moved
    const iris::SceneNodeType type = node->getSceneNodeType();
    if (type != iris::SceneNodeType::Mesh && type != iris::SceneNodeType::ParticleSystem) {
        mChecks.remove(node);
        return;
    }
    NodeCheck c;
    c.node = node->sharedFromThis().toWeakRef();
    QStringList missing;
    if (type == iris::SceneNodeType::Mesh) {
        auto *meshNode = static_cast<iris::MeshNode *>(node);
        if (node->isVisible() && node->getShadowCastingEnabled() && meshNode->getMesh()) {
            const iris::AABB local = meshNode->getMesh()->getAABB();
            const iris::Vec3 lo = local.getMin(), hi = local.getMax();
            const iris::Mat4 m = node->getGlobalTransform();
            iris::Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
            for (int k = 0; k < 8; ++k) {
                const iris::Vec3 p = m * iris::Vec3((k & 1) ? hi.x() : lo.x(), (k & 2) ? hi.y() : lo.y(),
                                                     (k & 4) ? hi.z() : lo.z());
                mn = iris::Vec3(qMin(mn.x(), p.x()), qMin(mn.y(), p.y()), qMin(mn.z(), p.z()));
                mx = iris::Vec3(qMax(mx.x(), p.x()), qMax(mx.y(), p.y()), qMax(mx.z(), p.z()));
            }
            c.blocker = true;
            c.mn = mn;
            c.mx = mx;
        }
        const iris::MaterialPtr material = meshNode->getMaterial();
        if (material) {
            c.material = material.toWeakRef();
            c.materialRevision = material->revision();
            for (auto it = material->textures.constBegin(); it != material->textures.constEnd(); ++it) {
                if (!it.value()) continue;
                const QString &path = it.value()->source;
                if (path.isEmpty() || path.startsWith(QLatin1Char(':')) ||
                    iris::LiveTextures::isLiveRef(path))
                    continue;
                c.paths << path;
                if (textureExists(path)) continue;
                missing << tr("%1 (%2)").arg(textureSlotName(it.key()), QFileInfo(path).fileName());
            }
            // A ROW THAT NAMES AN ASSET THE STORE COULD NOT RESOLVE (TEX-REF-1):
            // the reader keeps the guid with no file bound, so the next save
            // writes the same reference back — and this says so, by the asset
            // the scene names (there is no file name to show: there is no file).
            for (iris::Property *prop : material->properties) {
                if (!prop || prop->type != iris::PropertyType::Texture) continue;
                const QString guid = material->textureGuid(prop->name);
                if (guid.isEmpty() || material->textures.value(QStringLiteral("u_") + prop->name))
                    continue;
                missing << tr("%1 (asset %2: not in this library's store)")
                               .arg(textureSlotName(QStringLiteral("u_") + prop->name), guid);
            }
        }
        if (!missing.isEmpty()) {
            c.texture.kind = QStringLiteral("texture.missing");
            c.texture.node = node->getGUID();
            c.texture.nodeName = node->getName();
            c.texture.message = (missing.size() == 1
                ? tr("\"%1\" uses a texture file that is missing: %2.")
                : tr("\"%1\" uses texture files that are missing: %2."))
                                    .arg(node->getName(), missing.join(QStringLiteral(", ")));
            c.texture.action = tr("Re-import the missing file, or re-link the material's texture to a "
                                  "file that exists. Until then the surface renders without it.");
        }
    } else {
        // AN EMITTER'S IMAGE, the same rule (TEX-REF-1): a billboard image the
        // scene names by guid and the store could not resolve.
        auto *ps = static_cast<iris::ParticleSystemNode *>(node);
        if (!ps->textureGuid.isEmpty() && !ps->texture) {
            c.texture.kind = QStringLiteral("texture.missing");
            c.texture.node = ps->getGUID();
            c.texture.nodeName = ps->getName();
            c.texture.message = tr("\"%1\" uses a texture file that is missing: Particle "
                                   "Image (asset %2: not in this library's store).")
                                    .arg(ps->getName(), ps->textureGuid);
            c.texture.action = tr("Re-import the missing image, or pick another one for the "
                                  "emitter. Until then its particles draw untextured.");
        }
    }
    if (!c.texture.kind.isEmpty()) {
        c.texture.id = c.texture.kind + QLatin1Char(':') + c.texture.node;
        // THE WORDING FOLLOWS THE DISK: a second slot going missing, or one of
        // two coming back, is the same issue said again — updated in place,
        // never a second row (raise() of a live id is a no-op).
        if (indexOf(c.texture.id) >= 0) update(c.texture.id, c.texture.message);
        else raise(c.texture);
    }
    if (!c.blocker && c.material.isNull() && c.texture.id.isEmpty()) mChecks.remove(node);
    else mChecks.insert(node, c);
}

void SceneIssues::forgetSubtree(iris::SceneNode *node)
{
    // Anything that left may have been a blocker or a lamp: re-test.
    mLeakDirty = true;
    mChecks.remove(node);
    const int kids = node->childCount();
    for (int i = 0; i < kids; ++i)
        if (iris::SceneNode *c = node->childAt(i)) forgetSubtree(c);
}

// ---------------------------------------------------------------------------
// THE SCANNER
// ---------------------------------------------------------------------------
// Two conditions today, both owner-reported, both fixable by the person looking
// at the scene. Adding a third is: find it, build its id from its subject, and
// CLEAR the ids of that kind that no longer apply — the clearing half is the
// ONLY way a line ever leaves the bar (nothing is dismissible), and it is what
// stops a stale row naming a deleted object.
int SceneIssues::scan(const iris::ScenePtr &scene)
{
    if (!scene) { reset(); return 0; }

    QSet<QString> live;    // the ids this pass justifies
    ++mStats.passes;
    mStats.fullWalk = mStats.leakEvaluated = false;
    mStats.nodesVisited = 0;

    // ---- THE NODE CHECKS: what the document wrote since the last pass ------
    // This checker's cursor on the node graph's write journal names every
    // document node written since its last take (graph::WriteJournalReader,
    // with the journal's written half). A MOVED node is
    // re-checked with its subtree (its world pose moved theirs); a WRITTEN one
    // alone (a material swap, a flag, a parameter: its own answers); a node no
    // longer in this scene leaves the cache with its subtree. Then the nodes
    // whose material's revision moved, and those whose texture file the disk
    // reported. Nothing written = nothing visited.
    {
        std::vector<iris::SceneNode *> moved, written;
        int gone = 0;
        const bool complete = mJournal.take(moved, written, &gone);
        if (++mScansSinceRecheck >= kTextureRecheckScans) {
            mScansSinceRecheck = 0;
            startTextureRestat();
        }
        const iris::Scene *const here = scene.data();
        QSet<const iris::SceneNode *> visited;
        std::function<void(iris::SceneNode *)> walk = [&](iris::SceneNode *n) {
            if (!n || visited.contains(n)) return;
            visited.insert(n);
            checkNode(n);
            const int kids = n->childCount();
            for (int i = 0; i < kids; ++i) walk(n->childAt(i));
        };
        const auto inScene = [here](iris::SceneNode *n) { return n && n->getScene().data() == here; };
        if (mCheckedScene.toStrongRef() != scene || !complete) {
            // A NEW SCENE, or a write the journal cannot name (an engine-owned
            // node): the one whole walk.
            forgetWalks();
            mStats.fullWalk = true;
            walk(scene->getRootNode().data());
            mCheckedScene = scene;
            mMaterialsSeen = iris::Material::globalRevision();
            mFlippedPaths.clear();
        } else {
            // A node in ANOTHER document (a thumbnail's, a preview's) is not
            // ours and is skipped; one in NO document has left ours, with its
            // subtree.
            const auto departed = [](iris::SceneNode *n) { return n && n->getScene().isNull(); };
            for (iris::SceneNode *n : moved) {
                if (inScene(n)) walk(n);
                else if (departed(n)) forgetSubtree(n);
            }
            for (iris::SceneNode *n : written) {
                if (!inScene(n)) { if (departed(n)) forgetSubtree(n); continue; }
                if (visited.contains(n)) continue;
                visited.insert(n);
                checkNode(n);
            }
            // Destroyed since their write: their entries hold dead weak refs.
            if (gone) {
                for (auto it = mChecks.begin(); it != mChecks.end();) {
                    if (it->node.isNull()) { it = mChecks.erase(it); mLeakDirty = true; }
                    else ++it;
                }
            }
            QVector<iris::SceneNodePtr> again;
            const quint64 materials = iris::Material::globalRevision();
            if (materials != mMaterialsSeen) {
                mMaterialsSeen = materials;
                for (auto it = mChecks.cbegin(); it != mChecks.cend(); ++it) {
                    const iris::MaterialPtr m = it->material.toStrongRef();
                    if (m && m->revision() != it->materialRevision) {
                        if (iris::SceneNodePtr n = it->node.toStrongRef()) again << n;
                    }
                }
            }
            if (!mFlippedPaths.isEmpty()) {
                for (auto it = mChecks.cbegin(); it != mChecks.cend(); ++it)
                    for (const QString &path : it->paths)
                        if (mFlippedPaths.contains(path)) {
                            if (iris::SceneNodePtr n = it->node.toStrongRef()) again << n;
                            break;
                        }
                mFlippedPaths.clear();
            }
            for (const iris::SceneNodePtr &n : std::as_const(again)) {
                if (visited.contains(n.data()) || !inScene(n.data())) continue;
                visited.insert(n.data());
                checkNode(n.data());
            }
        }
    }
    // The ids up now, once per pass (raise() of a live id is a no-op, but it
    // asks by a linear search).
    QSet<QString> up;
    for (const SceneIssue &i : std::as_const(mIssues)) up.insert(i.id);
    const auto keepUp = [&](const SceneIssue &issue) {
        if (!up.contains(issue.id)) { raise(issue); up.insert(issue.id); }
        live.insert(issue.id);
    };

    // ---- sun.tie: two directional lights claim the same priority ----------
    // NOT "the scene has two directional lights" — that is normal and costs a
    // second directional term per pixel, nothing more, and the second one auto-
    // slots to priority 1 when it is added. The real defect is a TIE at the
    // lowest priority: then which light is the sun comes out of a tie-break
    // (creation order) that the author never chose, and the other one lights
    // the scene while casting nothing. Duplicating the sun produces exactly
    // this, because a duplicate copies the priority.
    {
        const auto dirs = scene->directionalLights();
        if (dirs.size() >= 2) {
            const int best = dirs.first()->forwardShadingPriority;
            const auto sun = scene->sunLight();
            for (const auto &light : dirs) {
                if (light == sun) continue;
                if (light->forwardShadingPriority != best) continue;
                SceneIssue issue;
                issue.kind = QStringLiteral("sun.tie");
                issue.node = light->getGUID();
                issue.nodeName = light->getName();
                issue.message =
                    tr("\"%1\" and \"%2\" are both set to forward shading priority %3, so only "
                       "one of them can be the sun.")
                        .arg(light->getName(), sun ? sun->getName() : tr("the sun"))
                        .arg(best);
                issue.action = tr("Give this light a different Forward Shading Priority. The "
                                  "lowest number is the sun and is the only directional light "
                                  "that casts a shadow.");
                issue.id = issue.kind + QLatin1Char(':') + issue.node;
                raise(issue);
                live.insert(issue.id);
            }
        }
    }

    // ---- sky.duplicate: a second Sky Light (SKY_LIGHT_SPEC.md §2) ---------
    // A scene's ambient is the FIRST VISIBLE Sky Light and nothing else, so a
    // second one is inert: it sits in the outliner, it has an intensity the
    // user can drag, and it does nothing at all. That is exactly the class of
    // thing this bar exists for — a scene the person looking at it can fix, in
    // one action, once they are told. (Duplicating the Sky Light is how it
    // happens; the fix is to delete or hide one.)
    {
        const auto skies = scene->skyLights();
        if (skies.size() >= 2) {
            const auto theSkylight = scene->skyLight();
            for (const auto &light : skies) {
                if (light == theSkylight) continue;
                if (!light->isVisibleInScene()) continue;   // hidden is the fix
                SceneIssue issue;
                issue.kind = QStringLiteral("sky.duplicate");
                issue.node = light->getGUID();
                issue.nodeName = light->getName();
                issue.message =
                    tr("\"%1\" is a second Sky Light. Only \"%2\" lights the scene — a scene "
                       "has one skylight.")
                        .arg(light->getName(),
                             theSkylight ? theSkylight->getName() : tr("the first one"));
                issue.action = tr("Delete this Sky Light, or hide it. A hidden Sky Light does "
                                  "nothing, which is how you keep one around without it "
                                  "competing.");
                issue.id = issue.kind + QLatin1Char(':') + issue.node;
                raise(issue);
                live.insert(issue.id);
            }
        }
    }

    // ---- shadow.leak: an unshadowed lamp reaching through something -------
    // The Showroom's lamp above a sealed roof lit the floor through it, and
    // nothing in the editor ever said so (SUN_AND_LIGHT_DEFAULTS §0). This is a
    // deliberately cheap PROXY — it reports a light that CAN shine through
    // something, not a proof that a pixel is wrong — so it names one blocker
    // per light: the NEAREST box its reach touches. The boxes are the node
    // checks' (each blocker's own world box, refreshed when the node is
    // written); the lamps are re-tested only when a check moved one.
    {
        QList<iris::LightNodePtr> leakers;
        for (const auto &light : scene->lights) {
            if (light.isNull() || !light->isVisible()) continue;
            if (light->lightType != iris::LightType::Point &&
                light->lightType != iris::LightType::Spot)
                continue;
            if (light->shadowMap && light->shadowMap->shadowType != iris::ShadowMapType::None)
                continue;
            leakers.append(light);
        }
        // A LAMP THAT IS GONE OR NOW CASTS takes its line with it, whatever
        // else is dirty: a deleted light is not a node any check visits.
        for (auto it = mLeakLines.begin(); it != mLeakLines.end();) {
            const bool still = std::any_of(leakers.cbegin(), leakers.cend(), [&](const iris::LightNodePtr &l) {
                return l->getGUID() == it->node;
            });
            if (still) ++it;
            else it = mLeakLines.erase(it);
        }
        if (leakers.isEmpty()) {
            mLeakLines.clear();
        } else if (mLeakDirty) {
            mStats.leakEvaluated = true;
            mLeakLines.clear();
            for (const auto &light : leakers) {
                const iris::Vec3 pos = light->getGlobalPosition();
                const float reach = qMax(0.01f, light->distance);
                const NodeCheck *nearest = nullptr;
                iris::SceneNodePtr nearestNode;
                float nearestD2 = 0.0f;
                for (auto it = mChecks.cbegin(); it != mChecks.cend(); ++it) {
                    const NodeCheck &c = *it;
                    if (!c.blocker) continue;
                    // Inside the thing is not "through" it (a lamp modelled into
                    // a fitting, a bulb inside a shade).
                    if (boxContains(pos, c.mn, c.mx)) continue;
                    if (!sphereTouchesBox(pos, reach, c.mn, c.mx)) continue;
                    const iris::SceneNodePtr n = c.node.toStrongRef();
                    if (!n) continue;
                    const float dx = qMax(0.0f, qMax(c.mn.x() - pos.x(), pos.x() - c.mx.x()));
                    const float dy = qMax(0.0f, qMax(c.mn.y() - pos.y(), pos.y() - c.mx.y()));
                    const float dz = qMax(0.0f, qMax(c.mn.z() - pos.z(), pos.z() - c.mx.z()));
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (nearest && (d2 > nearestD2 ||
                                    (d2 == nearestD2 && n->getGUID() >= nearestNode->getGUID())))
                        continue;
                    nearest = &c; nearestNode = n; nearestD2 = d2;
                }
                if (!nearest) continue;
                SceneIssue issue;
                issue.kind = QStringLiteral("shadow.leak");
                issue.node = light->getGUID();
                issue.nodeName = light->getName();
                issue.message = tr("\"%1\" casts no shadow, and its light reaches straight "
                                   "through \"%2\".")
                                    .arg(light->getName(), nearestNode->getName());
                issue.action = tr("Set this light's Shadow Type to Soft so the geometry blocks "
                                  "it, or reduce its Range so it no longer reaches that far.");
                issue.id = issue.kind + QLatin1Char(':') + issue.node;
                mLeakLines.insert(issue.id, issue);
            }
        }
        mLeakDirty = false;
        for (const SceneIssue &issue : std::as_const(mLeakLines)) keepUp(issue);
    }

    // ---- rays.absent: the project asked for rays this machine has not -----
    // The one issue here that is not about an OBJECT: it is about the project,
    // so it names no node and the bar shows it as a line with nothing to
    // select. That is exactly what the "On" state was added for (ledger §425) —
    // On and Auto render the identical picture, and the whole difference
    // between them is this sentence. Auto never raises it (falling back
    // silently is what Auto MEANS) and Off never raises it (a scene that asked
    // not to trace is not disappointed by a machine that cannot).
    {
        const int machine = machineRayTracing();
        if (scene->rayTracing == iris::RayTracingMode::On && machine == 0) {
            SceneIssue issue;
            issue.kind = QStringLiteral("rays.absent");
            issue.message = tr("This project expects hardware ray tracing; this machine has "
                               "none. It is rendering the fallback picture.");
            issue.action = tr("Nothing is broken — the scene renders without rays. Open it on a "
                              "machine with a ray-tracing GPU to see what it was authored for, "
                              "or set World > Ray Tracing to Auto if this project "
                              "should simply use rays wherever they exist.");
            issue.id = issue.kind;
            raise(issue);
            live.insert(issue.id);
        }
    }

    // ---- vr.colour: the runtime is encoding the picture a second time -----
    //
    // The other issue that is not about an object, and it belongs here for the
    // same reason `rays.absent` does: it is a property of the MACHINE (the
    // OpenXR runtime) that changes what the user is looking at, and nothing in
    // the scene can put it right. The engine hands display-encoded bytes to the
    // runtime and asks for an _SRGB swapchain, which is the format that says
    // so; a runtime that offers none is told the bytes are linear and encodes
    // them again, and the wearer's picture reads about a stop too bright.
    //
    // RAISED ONLY WHILE A SESSION RUNS, and it goes by itself when the session
    // ends (the kind is in the scanned list below) — which is the whole
    // contract of this bar: a line the user cannot dismiss, that leaves when
    // the condition does.
    {
        const auto engine = EngineHost::instance().engine();
        // The session as the last frame left it (ENGINE-UPGRADE-U3 stage 2: the snapshot).
        const jahshaka::engine::VrStatus vr = engine ? engine->published().vr
                                                     : jahshaka::engine::VrStatus();
        if (vr.active && !vr.colourEncodedOnce) {
            SceneIssue issue;
            issue.kind = QStringLiteral("vr.colour");
            issue.message = tr("The headset's colours are wrong: this VR runtime offers no "
                               "sRGB image format (it gave us %1), so it is brightening the "
                               "picture a second time. The desktop is correct.")
                                .arg(QString::fromStdString(vr.swapchainFormat));
            issue.action = tr("Update or change the VR runtime — nothing in this project "
                              "causes it, and nothing in the project can correct it.");
            issue.id = issue.kind;
            raise(issue);
            live.insert(issue.id);
        }
    }

    // ---- texture.missing: a material's texture file is gone from disk -----
    // The document KEEPS a material's path when its file disappears (moved,
    // deleted, a project opened on another machine), and the mirror's answer
    // to a path that does not exist is to bind nothing — the surface renders
    // untextured and the only trace was one engine error string nobody reads
    // (TEXTURE-FAIL-ISSUE-1). Decided per node by checkNode (below) when the
    // node, its material or one of its files changes; here the lines are kept.
    for (auto it = mChecks.cbegin(); it != mChecks.cend(); ++it)
        if (!it->texture.id.isEmpty()) keepUp(it->texture);

    // ---- clear what the scene no longer justifies -------------------------
    // Only the kinds this scanner owns: an issue raised by a verb or by another
    // producer is not ours to forget.
    static const QStringList kScanned{ QStringLiteral("sun.tie"), QStringLiteral("shadow.leak"),
                                       QStringLiteral("sky.duplicate"),
                                       QStringLiteral("rays.absent"),
                                       QStringLiteral("vr.colour"),
                                       QStringLiteral("texture.missing") };
    bool removed = false;
    for (int i = mIssues.size() - 1; i >= 0; --i) {
        if (!kScanned.contains(mIssues[i].kind)) continue;
        if (live.contains(mIssues[i].id)) continue;
        mIssues.removeAt(i);
        removed = true;            // ONE signal for the whole pass, not one per row
    }
    if (removed) emit changed();
    if (mStats.nodesVisited || mStats.leakEvaluated) ++mStats.walks;
    return mIssues.size();
}
