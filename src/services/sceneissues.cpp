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
#include <QVector>

#include <algorithm>

#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/sceneextents.h"

namespace {

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

/// Every mesh node under `root` that actually blocks light: visible, and a
/// shadow caster. The built-in GROUND is the one exclusion — it is in every
/// scene, it is under everything, and "your lamp shines through the Ground" is
/// never the sentence a user needs.
///
/// Identified by its flag AND its mesh path, the same belt-and-braces
/// sceneextents::describe uses and for the same reason: `isBuiltIn` is set on
/// any primitive the user ADDS and is not restored by the reader, so it is
/// useless as "this is the editor's own prop" (filtering on it made this
/// scanner blind to every cube and plane in the scene).
void collectBlockers(const iris::SceneNodePtr &node, QList<iris::SceneNodePtr> &out)
{
    if (!node) return;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh && node->isVisible() &&
        node->getShadowCastingEnabled()) {
        const auto mesh = node.staticCast<iris::MeshNode>();
        const bool isGround = mesh->defaultFloor ||
                              mesh->meshPath == QStringLiteral(":/models/ground.obj");
        if (!isGround) out.append(node);
    }
    for (const auto &child : node->children()) collectBlockers(child, out);
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

int SceneIssues::clearKind(const QString &kind)
{
    int n = 0;
    for (int i = mIssues.size() - 1; i >= 0; --i)
        if (mIssues[i].kind == kind) { mIssues.removeAt(i); ++n; }
    if (n) emit changed();
    return n;
}

void SceneIssues::reset()
{
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

    QStringList live;      // the ids this pass justifies

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
                live << issue.id;
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
                live << issue.id;
            }
        }
    }

    // ---- shadow.leak: an unshadowed lamp reaching through something -------
    // The Showroom's lamp above a sealed roof lit the floor through it, and
    // nothing in the editor ever said so (SUN_AND_LIGHT_DEFAULTS §0). This is a
    // deliberately cheap PROXY — it reports a light that CAN shine through
    // something, not a proof that a pixel is wrong — so it names one blocker
    // per light and stops.
    {
        QList<iris::SceneNodePtr> blockers;
        collectBlockers(scene->getRootNode(), blockers);
        // The boxes ONCE, not once per light: this runs on a timer in the
        // editor, and the owner's box is CPU-bound on the frame.
        struct Blocker { iris::SceneNodePtr node; iris::Vec3 mn, mx; };
        QVector<Blocker> boxes;
        boxes.reserve(blockers.size());
        for (const auto &blocker : blockers) {
            Blocker b;
            b.node = blocker;
            if (!sceneextents::worldAabb(QList<iris::SceneNodePtr>{ blocker }, false, b.mn, b.mx))
                continue;
            boxes.append(b);
        }
        for (const auto &light : scene->lights) {
            if (light.isNull() || !light->isVisible()) continue;
            if (light->lightType != iris::LightType::Point &&
                light->lightType != iris::LightType::Spot)
                continue;
            if (light->shadowMap && light->shadowMap->shadowType != iris::ShadowMapType::None)
                continue;
            const iris::Vec3 pos = light->getGlobalPosition();
            const float reach = qMax(0.01f, light->distance);
            for (const auto &b : boxes) {
                const iris::SceneNodePtr &blocker = b.node;
                // Inside the thing is not "through" it (a lamp modelled into a
                // fitting, a bulb inside a shade).
                if (boxContains(pos, b.mn, b.mx)) continue;
                if (!sphereTouchesBox(pos, reach, b.mn, b.mx)) continue;
                SceneIssue issue;
                issue.kind = QStringLiteral("shadow.leak");
                issue.node = light->getGUID();
                issue.nodeName = light->getName();
                issue.message = tr("\"%1\" casts no shadow, and its light reaches straight "
                                   "through \"%2\".")
                                    .arg(light->getName(), blocker->getName());
                issue.action = tr("Set this light's Shadow Type to Soft so the geometry blocks "
                                  "it, or reduce its Range so it no longer reaches that far.");
                issue.id = issue.kind + QLatin1Char(':') + issue.node;
                raise(issue);
                live << issue.id;
                break;
            }
        }
    }

    // ---- clear what the scene no longer justifies -------------------------
    // Only the kinds this scanner owns: an issue raised by a verb or by another
    // producer is not ours to forget.
    static const QStringList kScanned{ QStringLiteral("sun.tie"), QStringLiteral("shadow.leak"),
                                       QStringLiteral("sky.duplicate") };
    bool removed = false;
    for (int i = mIssues.size() - 1; i >= 0; --i) {
        if (!kScanned.contains(mIssues[i].kind)) continue;
        if (live.contains(mIssues[i].id)) continue;
        mIssues.removeAt(i);
        removed = true;            // ONE signal for the whole pass, not one per row
    }
    if (removed) emit changed();
    return mIssues.size();
}
