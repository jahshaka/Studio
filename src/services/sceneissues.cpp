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
    // "this run has no ray tracing": patch 0038 keeps the extensions off the
    // device entirely, so this run IS a machine without the hardware, and the
    // row's promise to say so applies to it.
    if (!engine->rayTracing()) return 0;
    return engine->rayQueryAvailable() ? 1 : 0;
}

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
            live << issue.id;
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
        const jahshaka::engine::VrStatus vr = engine ? engine->vrStatus()
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
            live << issue.id;
        }
    }

    // ---- exposure.legacy: an older exposure key was ignored ---------------
    // A file written before EXPOSURE-1 carries the retired chain-unit
    // `exposure`/`exposureMin`/`exposureMax` and nothing else. The same number
    // is a different picture in the two units, so the reader ignores them and
    // the scene opens at the constructor's grade — deliberately, and with no
    // migration (the CRUD law). This is the one consequence a user cannot see
    // in the picture, so it is said once, in a sentence, with the action that
    // ends it. The writer clears the flag: a scene saved from here on carries
    // its grade explicitly and the line goes with it.
    if (scene->legacyExposureKeyIgnored) {
        SceneIssue issue;
        issue.kind = QStringLiteral("exposure.legacy");
        issue.message = tr("This scene was saved with an older exposure setting, which is "
                           "measured differently and has been ignored. It is showing at the "
                           "default exposure.");
        issue.action = tr("Set the Exposure you want in World > Post Process and save. The "
                          "old value could not be converted: the same number means a "
                          "different picture in the two ways of measuring it.");
        issue.id = issue.kind;
        raise(issue);
        live << issue.id;
    }

    // ---- clear what the scene no longer justifies -------------------------
    // Only the kinds this scanner owns: an issue raised by a verb or by another
    // producer is not ours to forget.
    static const QStringList kScanned{ QStringLiteral("sun.tie"), QStringLiteral("shadow.leak"),
                                       QStringLiteral("sky.duplicate"),
                                       QStringLiteral("rays.absent"),
                                       QStringLiteral("vr.colour"),
                                       QStringLiteral("exposure.legacy") };
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
