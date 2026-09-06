/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/avatar/api/avatarapi.h"

#include <QColor>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>

#include "modules/avatar/avatarpreviewmodel.h"
#include "modules/avatar/avatarsockets.h"
#include "irisgl/document/animation/locomotion.h"
#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/input/inputmap.h"
#include "irisgl/document/input/possession.h"
#include "scripting/modules/moduleshared.h"
#include "data/database/database.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "commands/nodeeditcommand.h"

AvatarApi::AvatarApi(ScriptHost &host, avatar::AvatarPreviewModel *model, QObject *parent)
    : ApiModule(host, parent), mModel(model)
{
}

QVector<VerbInfo> AvatarApi::verbs() const
{
    return {
        { "loadPreview", "avatar.loadPreview(path) -> {name, file, bones, meshes, vertices, influences, clips:[{name, rawName, length}]}",
          "Loads a rigged model file (fbx/glb/obj/...) into the Avatar page's own preview — no library row, no project pin, no database write, no undo command. Embedded textures are extracted to a per-session scratch dir. Replaces whatever was loaded (one subject at a time).",
          Needs::Document },
        { "loadAnimation", "avatar.loadAnimation(path) -> {file, name, added, clips:[...], match:{channels, boneChannels, matched}}",
          "Loads a SEPARATE animation file onto the character already in the preview and appends its clips to the list — the Mixamo workflow (one character download, then one file per animation). Accepts both export shapes: a with-skin animation file (its mesh is ignored) and an animation-only file (zero meshes, which the mesh loaders reject outright). Clips accumulate; nothing is switched — call avatar.setClip to play one. Clip names come from the ANIMATION file's base name when the file uses a junk name, which every Mixamo export does. THROWS when the file animates a different rig (the clip->bone join is by scene-node name, so a foreign clip would load and move nothing): the message names the bones that do not exist on the loaded rig.",
          Needs::Document },
        { "clearPreview", "avatar.clearPreview() -> bool",
          "Removes the previewed model and deletes its scratch extract dir.",
          Needs::Document },
        { "preview", "avatar.preview() -> {name, file, bones, meshes, vertices, influences, clips, activeClip, time, duration, playing, looping, meshVisible, skeletonVisible} | undefined",
          "Everything the page shows about the loaded model, including transport and toggle state. Undefined (falsy) when nothing is loaded.",
          Needs::Document },
        { "setMeshVisible", "avatar.setMeshVisible(on) -> bool",
          "Shows or hides the skinned mesh. Independent of the skeleton toggle: all four combinations are valid.",
          Needs::Document },
        { "setSkeletonVisible", "avatar.setSkeletonVisible(on) -> bool",
          "Shows or hides the bone-line overlay. Independent of the mesh toggle.",
          Needs::Document },
        { "clips", "avatar.clips() -> [{name, rawName, length, looping, active, source, external}]",
          "Every clip the preview knows about: the ones the character file carried, plus every one avatar.loadAnimation has added since (`external`, with `source` naming the file it came from). `name` is the display name: every Mixamo clip is literally called 'mixamo.com', so junk names fall back to the source file's base name (`rawName` keeps what the file said).",
          Needs::Document },
        { "history", "avatar.history() -> [{file, name, loaded}]",
          "The character files loaded in this session — what the page's left column lists. Session-local and not persisted (the avatar library is Part 1's).",
          Needs::Document },
        { "forget", "avatar.forget(path) -> bool",
          "Drops a file from the session list (the left column's right-click Delete). Clears the preview when it is the loaded one. Deletes nothing on disk.",
          Needs::Document },
        { "setRootMotion", "avatar.setRootMotion(on) -> bool",
          "Root motion for the preview. Off (the default) plays locomotion clips IN PLACE — the horizontal translation of the clip's root-most animated bone is pinned to its first key, so a walk cycle walks on the spot instead of leaving the frame. On plays the clip exactly as authored. Vertical motion is never stripped, so a jump still leaves the ground.",
          Needs::Document },
        { "spaceMode", "avatar.spaceMode(mode?) -> 'grid' | 'modern'",
          "The avatar page's 3D environment (AVATAR_SPACE_SPEC). With no argument, returns the current mode. With one, switches it: 'grid' is the founding minimal look; 'modern' is the Tron room — a 10x10 grid of mirror-black floor tiles over dead-black seams, white walls whose seams glow, and a softly emissive ceiling. The choice persists across sessions. Throws on an unknown mode name.",
          Needs::Document },
        { "setClip", "avatar.setClip(name) -> bool",
          "Makes `name` (display or raw) the active clip and rewinds to 0 — including a clip loaded from a separate file by avatar.loadAnimation. What the ANIMATIONS list double-click calls. The transport state carries over: switching while playing keeps playing, from the start of the new clip. Every bone is put back on its rest pose first, so a clip that does not mention a bone cannot inherit the previous clip's pose for it.",
          Needs::Document },
        { "playClip", "avatar.playClip(name) -> bool",
          "Starts the preview transport. With a name, selects that clip first; without one, resumes the active clip. Drives the module's OWN preview document — never the editor scene's clock.",
          Needs::Document },
        { "pause", "avatar.pause() -> bool", "Stops advancing time, keeping the current pose.", Needs::Document },
        { "stop", "avatar.stop() -> bool", "Pauses and rewinds to time 0.", Needs::Document },
        { "setLooping", "avatar.setLooping(on) -> bool", "Loops the active clip (default on).", Needs::Document },
        { "setTime", "avatar.setTime(seconds) -> bool",
          "Scrubs the preview to `seconds` and re-evaluates the pose immediately.",
          Needs::Document },
        { "time", "avatar.time() -> number", "The preview's current time in seconds.", Needs::Document },
        { "bones", "avatar.bones() -> [{name, parent, position:{x,y,z}}]",
          "The rig as the preview resolves it: one entry per bone that has a scene node, `parent` being the NEAREST ancestor that is also a bone (assimp pivot nodes sit between real bones, and Bone::parentBone is empty for such rigs). World-space positions AT THE CURRENT TIME, read back from the engine's evaluated skeleton — clip evaluation is the engine's, so a pose only exists where an engine does. Under --headless the rig's shape (names, parents, hierarchy) is still reported but the positions are the REST pose.",
          Needs::Engine },
        { "snapshot", "avatar.snapshot(path, w=256, h=256, probes=[]) -> {path, width, height, center:{r,g,b}, probes:[{x,y,r,g,b}]}",
          "Offscreen render of the Avatar page's preview scene to a PNG, with the centre pixel and each probe point ({x,y} normalized 0..1) returned so scripts can assert on colours — the way a script (or an MCP session) proves the skeleton-only view from outside the app.",
          Needs::Engine },
        { "addSockets", "avatar.addSockets(nodeId) -> [{socket, bone, mapped, existed, node}]",
          "Installs this module's BUILT-IN sockets on a character in the OPEN SCENE "
          "(CAMERAS_SPEC D9): `head` for first person — the camera an agent driving the avatar "
          "sees through — and `shoulder` for third person, mapped from the Mixamo-class bone "
          "names the module already understands (mixamorig:, Biped's Bip01, VRM's J_Bip, bare "
          "and _JNT-suffixed spellings; the right shoulder is preferred over the left). "
          "GIVEN A MESH NODE it maps against that node's own rig; given anything else (a "
          "character's wrapper — what avatar.spawn returns) it sweeps the SUBTREE and puts each "
          "socket on the skinned piece whose rig actually has the bone, because a real export is "
          "several pieces bound to subsets of one skeleton (Mixamo's Beta: the limbs piece has "
          "neither Head nor Shoulder). `node` in each row is the piece the socket landed on. "
          "FAILS SOFT: a rig with no recognizable head simply gets no head socket — the report "
          "says which built-in mapped to which bone and which did not, instead of throwing. A "
          "socket the character ALREADY has is left alone (`existed`), so re-installing never "
          "discards an authored offset. The built-in offsets are IDENTITY: a socket's offset is "
          "in bone space, and bone axes and world scale differ per rig (a Mixamo character is "
          "~170 units tall, its glTF export ~1.7), so any hard-coded framing offset would be "
          "silently wrong on half the files — author it with node.addSocket. This is the one "
          "avatar verb that touches the editor scene rather than the module's own preview. "
          "Undoable.",
          Needs::Document },
        { "spawn", "avatar.spawn(assetGuid, {position?, parent?}) -> nodeId",
          "Instantiates an OBJECT asset from the open project as a playable AVATAR "
          "(AVATAR_LOCOMOTION_SPEC §6): the same instantiation assets.addToScene does, plus the "
          "movement component on the wrapper node with its capsule fitted (in WORLD metres) to "
          "the character's geometry, plus this module's built-in head/shoulder sockets, each "
          "installed on whichever skinned piece inside the character actually carries its bone. "
          "`position` places it; `parent` is a node id to spawn under (default: the scene root). "
          "Spawning DURING play registers the avatar with the running physics world on the spot "
          "rather than refusing (spec R6), so a scripted spawn mid-play walks immediately. "
          "The knobs afterwards are avatar.movement / avatar.setMovement. Undoable.",
          Needs::Document },
        { "movement", "avatar.movement(nodeId) -> {walkSpeed, runSpeed, maxAcceleration, brakingDeceleration, groundFriction, jumpVelocity, jumpCount, coyoteTime, jumpReArm, airControl, gravityScale, maxStepHeight, walkableFloorAngle, orientRotationToMovement, rotationRate, capsuleAuto, capsuleRadius, capsuleHeight}",
          "The movement knobs on an avatar node (spec §6.2). Empty (falsy) for a node that carries "
          "no avatar component — which is every node but an avatar wrapper.",
          Needs::Document },
        { "setMovement", "avatar.setMovement(nodeId, {...}) -> object",
          "Writes some or all of the movement knobs and returns the RESOLVED set, which is what "
          "actually took: values are clamped to sane ranges rather than refused (a negative "
          "walkSpeed becomes 0, walkableFloorAngle is capped at 89 deg so a wall can never read as "
          "floor, and the capsule is never shorter than a sphere of its own radius). Writing "
          "capsuleRadius or capsuleHeight clears capsuleAuto — an explicit dimension is a decision, "
          "and re-deriving it from the mesh would silently discard it. Unknown keys are REFUSED, "
          "not ignored. Undoable.",
          Needs::Document },
        { "input", "avatar.input({move:{x,y}, look:{x,y}, jump, sprint}) -> {move, look, jump, sprint}",
          "Writes the gameplay input state directly — the SCRIPTED producer, and the reason "
          "locomotion can be tested with no window and no synthetic key events. Every field is "
          "optional and an absent field is left alone. `move` is raw intent (camera-relative "
          "rotation is possession's job) and is clamped to the unit disc. `jump:true` SETS the "
          "one-shot latch and never clears it — clearing is the movement step's job, so a script "
          "cannot silently swallow a pending jump; `jump:false` is therefore not a clear, it is a "
          "no-op. `sprint` is a plain held bool. Returns the resulting state, the same shape "
          "input.state() reports. A real key event afterwards recomputes move/sprint from the "
          "held keys and overwrites what this wrote — last producer wins.",
          Needs::Document },

        // ---- possession (AVATAR_LOCOMOTION_SPEC §8.4, Stage 3) ------------
        { "possess", "avatar.possess(nodeId) -> bool",
          "Hands the gameplay input to ONE avatar — the Unreal model, stripped to what a single "
          "local player needs: one possession slot per scene, no controller actors, no player "
          "index. The possessed avatar is the only consumer of the input state, and its move "
          "intent is rotated by the follow camera's yaw (so 'forward' means 'away from the "
          "camera'). Possessing B while A is possessed IMPLICITLY UNPOSSESSES A first, which "
          "also ZEROES A's input — otherwise a W held at the moment of the switch would walk A "
          "forever. Every unpossessed avatar keeps stepping with zero input, so it idles rather "
          "than freezing at bind pose. Refused for a node with no avatar component. RUNTIME "
          "ONLY: which avatar you were driving is never written to the file, and it is dropped "
          "when play stops. Not undoable — it is play state, not a document edit.",
          Needs::Document },
        { "unpossess", "avatar.unpossess() -> bool",
          "Releases the possessed avatar and zeroes its input; it keeps stepping and idling. "
          "False (and harmless) when nothing was possessed, which is what makes a redundant "
          "editor.stop() free.",
          Needs::Document },
        { "possessed", "avatar.possessed() -> nodeId | null",
          "The avatar the input is driving, or null. Also null once that node has been deleted — "
          "the slot holds a weak reference on purpose.",
          Needs::Document },
        { "list", "avatar.list() -> [{id, name, possessed, speed, grounded, mode, state}]",
          "Every node in the scene carrying an avatar component, in DOCUMENT ORDER (depth-first "
          "from the root) — the same order Play's auto-possess is defined against, so list()[0] "
          "is the avatar scene.playMode('third-person') takes over. `speed`, `grounded` and "
          "`mode` are the movement component's published state (spec §5); `state` is the "
          "locomotion state machine's current state, an empty string for an avatar whose clips "
          "bound no role (which is not an error — the default asset degrades, it never refuses "
          "to load).",
          Needs::Document },
        { "followCamera", "avatar.followCamera({armLength?, heightOffset?, lateralOffset?, yaw?, pitch?}) -> object",
          "The spring-arm follow camera (spec §8.5). With no argument it reports the current arm; "
          "any field writes it. The arm is anchored to the avatar WRAPPER's transform, not to a "
          "bone: a shoulder bone bobs with every footfall, which is right for over-the-shoulder "
          "aiming and wrong for a smooth follow. The `shoulder` socket is still what sizes it — "
          "read ONCE at possess time as the offset reference, never per frame. `yaw`/`pitch` are "
          "the live angles the Look action drives, in degrees; pitch is clamped to the arm's "
          "limits. Reports `position` and `rotation` too, which is what makes camera-relative "
          "movement assertable from a script.",
          Needs::Document },

        // ---- the state machine (AVATAR_LOCOMOTION_SPEC §7, Stage 4) -------
        { "locomotionState", "avatar.locomotionState(nodeId) -> {speed, moveInput:{x,y,z}, grounded, verticalVelocity, jumpRequested, mode, state, previousState, transitioning, transitionProgress, transitionDuration, phase, weights:[{clip, weight, time, looping}]}",
          "The five-parameter contract the movement component publishes every step (spec §5), "
          "plus what the state machine did with it. `state` is the state the machine is IN — "
          "during a cross-fade that is already the DESTINATION, with `previousState` naming what "
          "is blending out and `transitionProgress` running 0 -> 1 over `transitionDuration` "
          "seconds. `weights` is the per-frame push: one entry per clip, with the weight the "
          "state machine intends and the ABSOLUTE time in seconds it should be sampled at — "
          "never a relative advance, so two avatars that started walking two seconds apart are "
          "never in lock-step. The weights are raw INTENT and are not normalized here: the "
          "engine normalizes them per bone. Empty (falsy) for a node with no avatar component.",
          Needs::Document },
        { "locomotionAsset", "avatar.locomotionAsset(nodeId) -> {name, entry, default, states:[...], transitions:[...]}",
          "The state machine as DATA (spec §7.1) — the exact shape setLocomotionAsset accepts "
          "and the exact shape the scene file stores, so a round trip is an equality check. A "
          "state is {name, loop, source} where source is either {kind:'clip', clip, speed} or "
          "{kind:'blendspace', syncClips, samples:[{clip, position, authoredSpeed}]}; a "
          "transition is {from, to, condition, blendDuration}, and TRANSITION ORDER IS THE "
          "PRIORITY — first match wins, there is no priority field and there never will be. "
          "`from` may be '*' for any state, which means any OTHER state: a wildcard does not "
          "speak for a state that already carries an explicit transition to the same "
          "destination. `default` is true while the asset is the generated 'Biped Locomotion' "
          "one, which is what lets a role rebind or a walkSpeed change regenerate it without "
          "ever overwriting an asset somebody authored.",
          Needs::Document },
        { "setLocomotionAsset", "avatar.setLocomotionAsset(nodeId, {name?, entry, states, transitions}) -> bool",
          "Installs an authored state machine, replacing the generated default. REFUSED, with "
          "the reason, on: a condition outside the CLOSED vocabulary (the message names the "
          "offending text and lists the six forms — `speed > x`, `speed < x`, `grounded`, "
          "`!grounded`, `clipEnded(fraction)`, `jumpRequested`), a `from`/`to`/`entry` that "
          "names no state, a state that resolves to no clip (an empty clip set is a FROZEN "
          "pose, not a bind pose), a blend space with fewer than two samples or with sample "
          "positions that are not strictly increasing, a negative blend duration, or a "
          "transition from a state to itself. Nothing changes on a refusal. Undoable.",
          Needs::Document },
        { "clipRoles", "avatar.clipRoles(nodeId) -> {idle, walk, run, jumpStart, fallLoop, land, clips:[{name, length}]}",
          "The role -> clip-name binding the default asset is built from (spec §7.4), plus the "
          "clips the character actually carries. Populated by TOLERANT NAME MATCHING at spawn "
          "and re-run whenever a clip is loaded onto an already-spawned character: it works on "
          "words rather than equality because Mixamo names are what they are, and it settles "
          "the overlaps by resolving land -> jump-start -> fall-loop -> run -> walk -> idle "
          "(so 'Falling To Landing' is a landing and 'Falling Idle' is a fall). An UNBOUND role "
          "is an empty string and that is not an error: the default asset degrades — no `run` "
          "gives a two-sample blend space, no `land` skips the Land state entirely.",
          Needs::Document },
        { "setClipRole", "avatar.setClipRole(nodeId, role, clipName) -> bool",
          "Binds one role by hand: `idle`, `walk`, `run`, `jumpStart` (or `jump-start`), "
          "`fallLoop` (or `fall-loop`), `land`. An empty clipName UNBINDS the role, which is "
          "how you tell the default asset to skip a state. A hand-bound role is never clobbered "
          "by a later automatic re-match. Regenerates the default asset when that is what is "
          "installed, and leaves an authored asset alone. Undoable.",
          Needs::Document },
    };
}

void AvatarApi::notifyChanged()
{
    if (mChanged) mChanged();
}

void AvatarApi::notifySubjectChanged()
{
    if (mSubjectChanged) mSubjectChanged();
    notifyChanged();
}

QVariantMap AvatarApi::previewState() const
{
    QVariantMap out;
    out["name"] = mModel->name();
    out["file"] = mModel->filePath();
    out["bones"] = mModel->boneCount();
    out["meshes"] = mModel->meshCount();
    out["vertices"] = mModel->vertexCount();
    out["influences"] = mModel->influencesPerVertex();
    out["hasSkeleton"] = mModel->hasSkeleton();
    QVariantList clipList;
    for (const auto &c : mModel->clips())
        clipList.append(QVariantMap{ { "name", c.name }, { "rawName", c.rawName },
                                     { "length", c.length }, { "looping", c.looping },
                                     { "active", c.active }, { "source", c.source },
                                     { "external", c.external } });
    out["clips"] = clipList;
    out["rootMotion"] = mModel->rootMotion();
    out["activeClip"] = mModel->activeClip();
    out["duration"] = mModel->duration();
    out["time"] = mModel->time();
    out["playing"] = mModel->isPlaying();
    out["looping"] = mModel->looping();
    out["meshVisible"] = mModel->meshVisible();
    out["skeletonVisible"] = mModel->skeletonVisible();
    return out;
}

QVariant AvatarApi::loadPreview(const QString &path)
{
    if (!mModel) { fail("avatar: not available in this session"); return QVariant(); }
    if (path.trimmed().isEmpty()) { fail("avatar.loadPreview: a file path is required"); return QVariant(); }
    QString error;
    if (!mModel->load(path, &error)) { fail(QStringLiteral("avatar.loadPreview: %1").arg(error)); return QVariant(); }
    notifySubjectChanged();
    return previewState();
}

bool AvatarApi::record(const QString &message)
{
    mLastError = message;
    return fail(message);
}

QVariant AvatarApi::loadAnimation(const QString &path)
{
    mLastError.clear();
    if (!mModel) { record("avatar: not available in this session"); return QVariant(); }
    if (path.trimmed().isEmpty()) { record("avatar.loadAnimation: a file path is required"); return QVariant(); }
    QString error;
    avatar::ClipLoadReport report;
    if (!mModel->loadAnimation(path, &error, &report)) {
        record(QStringLiteral("avatar.loadAnimation: %1").arg(error));
        return QVariant();
    }
    // The clip list changed but the subject did not: no re-framing (the
    // camera must not jump when a user adds a second walk cycle).
    notifyChanged();
    QVariantMap out = previewState();
    out["file"] = QFileInfo(path).absoluteFilePath();
    out["added"] = report.added;
    out["clip"] = report.firstClip;
    out["match"] = QVariantMap{ { "channels", report.channels },
                                { "boneChannels", report.boneChannels },
                                { "matched", report.matched } };
    return out;
}

QVariantList AvatarApi::history()
{
    QVariantList out;
    if (!mModel) { fail("avatar: not available in this session"); return out; }
    const QString loaded = mModel->filePath();
    for (const QString &file : mModel->history())
        out.append(QVariantMap{ { "file", file },
                                { "name", QFileInfo(file).fileName() },
                                { "loaded", file == loaded } });
    return out;
}

bool AvatarApi::forget(const QString &path)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->forget(path))
        return fail(QStringLiteral("avatar.forget: '%1' is not in the session list").arg(path));
    notifySubjectChanged();
    return true;
}

bool AvatarApi::setRootMotion(bool on)
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->setRootMotion(on);
    notifyChanged();
    return true;
}

QVariant AvatarApi::spaceMode(const QVariant &mode)
{
    if (!mModel) { fail("avatar: not available in this session"); return QVariant(); }
    if (!mode.isValid() || mode.toString().isEmpty())
        return QString::fromLatin1(avatar::space::modeName(mModel->spaceMode()));

    avatar::SpaceMode m;
    if (!avatar::space::parseMode(mode.toString(), &m)) {
        fail(QString("avatar.spaceMode: unknown mode '%1' — the modes are "
                     "'grid' and 'modern'").arg(mode.toString()));
        return QVariant();
    }
    if (!mModel->setSpaceMode(m)) {
        fail("avatar.spaceMode: the room's geometry resources are not "
             "available in this session");
        return QVariant();
    }
    if (mPersistMode) mPersistMode(avatar::space::modeName(m));
    notifyChanged();
    return QString::fromLatin1(avatar::space::modeName(mModel->spaceMode()));
}

bool AvatarApi::clearPreview()
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->clear();
    notifySubjectChanged();
    return true;
}

QVariant AvatarApi::preview()
{
    if (!mModel || !mModel->isLoaded()) return QVariant();
    return previewState();
}

bool AvatarApi::setMeshVisible(bool on)
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->setMeshVisible(on);
    notifyChanged();
    return true;
}

bool AvatarApi::setSkeletonVisible(bool on)
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->setSkeletonVisible(on);
    notifyChanged();
    return true;
}

QVariantList AvatarApi::clips()
{
    QVariantList out;
    if (!mModel) { fail("avatar: not available in this session"); return out; }
    for (const auto &c : mModel->clips())
        out.append(QVariantMap{ { "name", c.name }, { "rawName", c.rawName },
                                { "length", c.length }, { "looping", c.looping },
                                { "active", c.active }, { "source", c.source },
                                { "external", c.external } });
    return out;
}

bool AvatarApi::setClip(const QString &name)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.setClip: nothing is loaded");
    if (!mModel->setClip(name))
        return fail(QStringLiteral("avatar.setClip: no clip named '%1'").arg(name));
    notifyChanged();
    return true;
}

bool AvatarApi::playClip(const QString &name)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.playClip: nothing is loaded");
    if (!name.isEmpty() && !mModel->setClip(name))
        return fail(QStringLiteral("avatar.playClip: no clip named '%1'").arg(name));
    if (mModel->clips().isEmpty()) return fail("avatar.playClip: this file has no clips");
    mModel->play();
    notifyChanged();
    return true;
}

bool AvatarApi::pause()
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->pause();
    notifyChanged();
    return true;
}

bool AvatarApi::stop()
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->stop();
    notifyChanged();
    return true;
}

bool AvatarApi::setLooping(bool on)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.setLooping: nothing is loaded");
    mModel->setLooping(on);
    notifyChanged();
    return true;
}

bool AvatarApi::setTime(double seconds)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.setTime: nothing is loaded");
    mModel->setTime(float(seconds));
    notifyChanged();
    return true;
}

double AvatarApi::time()
{
    if (!mModel) { fail("avatar: not available in this session"); return 0.0; }
    return double(mModel->time());
}

QVariantList AvatarApi::bones()
{
    QVariantList out;
    if (!mModel) { fail("avatar: not available in this session"); return out; }
    // The pose lives in the engine's evaluated skeleton, and the engine
    // evaluates during a render — so a script that sets a time and asks for a
    // bone in the next statement would otherwise read the previous frame.
    if (mResolvePose) mResolvePose();
    for (const auto &b : mModel->bones()) {
        out.append(QVariantMap{
            { "name", b.name }, { "parent", b.parent },
            { "position", QVariantMap{ { "x", b.position.x() }, { "y", b.position.y() },
                                       { "z", b.position.z() } } } });
    }
    return out;
}

QVariantMap AvatarApi::snapshot(const QString &path, int width, int height,
                                const QVariantList &probes)
{
    QVariantMap out;
    if (!mSnapshot) { fail("avatar.snapshot: the avatar preview is not running in this session"); return out; }
    if (path.isEmpty()) { fail("avatar.snapshot: a file path is required"); return out; }

    const QImage img = mSnapshot(qBound(16, width, 4096), qBound(16, height, 4096));
    if (img.isNull()) { fail("avatar.snapshot: the preview returned no image"); return out; }

    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(".");
    if (!img.save(path, "PNG")) {
        fail(QStringLiteral("avatar.snapshot: could not save '%1'").arg(path));
        return out;
    }

    const QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
    out["path"] = info.absoluteFilePath();
    out["width"] = img.width();
    out["height"] = img.height();
    out["center"] = QVariantMap{ { "r", center.red() }, { "g", center.green() }, { "b", center.blue() } };

    // Same probe convention as editor.screenshot: normalized 0..1 coordinates,
    // each returning the average of the 5x5 block so assertions survive minor
    // framing drift across drivers.
    QVariantList probeResults;
    for (const QVariant &p : probes) {
        const QVariantMap pm = p.toMap();
        const double px = qBound(0.0, pm.value("x").toDouble(), 1.0);
        const double py = qBound(0.0, pm.value("y").toDouble(), 1.0);
        const int ix = qMin(int(px * img.width()), img.width() - 1);
        const int iy = qMin(int(py * img.height()), img.height() - 1);
        int r = 0, g = 0, b = 0, n = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = ix + dx, y = iy + dy;
                if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
                const QColor c = img.pixelColor(x, y);
                r += c.red(); g += c.green(); b += c.blue(); ++n;
            }
        }
        if (n > 0) { r /= n; g /= n; b /= n; }
        probeResults.append(QVariantMap{ { "x", px }, { "y", py }, { "r", r }, { "g", g }, { "b", b } });
    }
    if (!probeResults.isEmpty()) out["probes"] = probeResults;
    return out;
}

// --- built-in sockets (CAMERAS_SPEC D9) ------------------------------------
//
// The ONE verb here that works on the editor scene rather than on the module's
// own preview document. It lives on `avatar` and not on `node` because what it
// carries is avatar knowledge — the Mixamo-class bone-name table
// (avatarsockets.h) — and the node module has no business importing a feature
// module. The sockets it installs are ordinary generic sockets; everything
// afterwards (attach, offset, remove) is node.*.
namespace
{
/// Does anything under (or at) this node carry a rig? Only used to tell a
/// rig-less subtree ("that is a rock") from a rigged one whose joints simply
/// do not canonicalize (fail-soft, two unmapped rows).
bool subtreeHasRig(const iris::SceneNodePtr &node)
{
    if (node.isNull()) return false;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh &&
        node.staticCast<iris::MeshNode>()->hasSkeleton())
        return true;
    const int kids = node->childCount();
    for (int i = 0; i < kids; ++i)
        if (iris::SceneNode *c = node->childAt(i))
            if (subtreeHasRig(c->sharedFromThis())) return true;
    return false;
}
}   // namespace

QVariantList AvatarApi::addSockets(const QString &nodeId)
{
    QVariantList out;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) { record("avatar.addSockets: no scene is open"); return out; }
    auto node = scriptmod::findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) {
        record(QStringLiteral("avatar.addSockets: no node with id '%1'").arg(nodeId));
        return out;
    }
    // A MESH node keeps the per-node semantics (its own rig, its own sockets).
    // Anything else — a character's wrapper, the node avatar.spawn returns — is
    // treated as the character it is and swept subtree-wide: a real export is
    // several skinned pieces sharing one skeleton, and which piece carries the
    // head chain is not something a caller can be asked to know.
    QVector<avatar::sockets::Mapping> report;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto mesh = node.staticCast<iris::MeshNode>();
        if (!mesh->hasSkeleton()) {
            record(QStringLiteral("avatar.addSockets: '%1' has no rig — nothing to map "
                                  "(node.boneNames lists a node's bones)").arg(node->getName()));
            return out;
        }
        report = avatar::sockets::installBuiltIns(mesh);
    } else {
        report = avatar::sockets::installBuiltInsInSubtree(node);
        bool anyRig = false;
        for (const auto &mapping : report) if (mapping.owner) anyRig = true;
        if (!anyRig && !subtreeHasRig(node)) {
            record(QStringLiteral("avatar.addSockets: nothing under '%1' has a rig — nothing to "
                                  "map (node.boneNames lists a node's bones)").arg(node->getName()));
            return out;
        }
    }

    // Undo removes exactly the sockets THIS call created — not the ones that
    // were already there, whose offsets the user may have authored — and from
    // the PIECE each landed on, which the subtree sweep chose.
    QVector<QPair<iris::MeshNodePtr, QString>> created;
    for (const auto &mapping : report)
        if (mapping.mapped && !mapping.existed && mapping.owner)
            created.append({ mapping.owner, mapping.socket });
    if (!created.isEmpty() && host.services && host.services->undo) {
        QVector<QPair<iris::MeshNodePtr, iris::Socket>> sockets;
        for (const auto &row : created)
            if (const iris::Socket *s = row.first->findSocket(row.second))
                sockets.append({ row.first, *s });
        host.services->undo->push(new NodeEditCommand(
            QStringLiteral("avatar sockets"),
            [sockets]() { for (const auto &row : sockets) row.first->addSocket(row.second); },
            [created]() { for (const auto &row : created) row.first->removeSocket(row.second); }));
    }

    for (const auto &mapping : report) {
        out.append(QVariantMap{ { "socket", mapping.socket },
                                { "bone", mapping.bone },
                                { "mapped", mapping.mapped },
                                { "existed", mapping.existed },
                                { "node", mapping.ownerGuid } });
    }
    return out;
}


// ---------------------------------------------------------------------------
// AVATAR_LOCOMOTION_SPEC Stage 2 — the movement component's verbs (§10).

namespace
{
/// The §6.2 knob set as JSON. One place, so `movement` and `setMovement`'s
/// return value cannot drift apart.
QVariantMap paramsToJs(const iris::AvatarMovementParams &p)
{
    return {
        { "walkSpeed", p.walkSpeed },
        { "runSpeed", p.runSpeed },
        { "maxAcceleration", p.maxAcceleration },
        { "brakingDeceleration", p.brakingDeceleration },
        { "groundFriction", p.groundFriction },
        { "jumpVelocity", p.jumpVelocity },
        { "jumpCount", p.jumpCount },
        { "coyoteTime", p.coyoteTime },
        { "jumpReArm", p.jumpReArm },
        { "airControl", p.airControl },
        { "gravityScale", p.gravityScale },
        { "maxStepHeight", p.maxStepHeight },
        { "walkableFloorAngle", p.walkableFloorAngle },
        { "orientRotationToMovement", p.orientRotationToMovement },
        { "rotationRate", p.rotationRate },
        { "capsuleAuto", p.capsuleAuto },
        { "capsuleRadius", p.capsuleRadius },
        { "capsuleHeight", p.capsuleHeight },
    };
}

const QStringList &knobNames()
{
    static const QStringList names = {
        "walkSpeed", "runSpeed", "maxAcceleration", "brakingDeceleration", "groundFriction",
        "jumpVelocity", "jumpCount", "coyoteTime", "jumpReArm", "airControl", "gravityScale",
        "maxStepHeight", "walkableFloorAngle", "orientRotationToMovement", "rotationRate",
        "capsuleAuto", "capsuleRadius", "capsuleHeight"
    };
    return names;
}

}   // namespace

QString AvatarApi::spawn(const QString &assetGuid, const QVariantMap &options)
{
    if (!host.db || !host.services || !host.services->sceneEdit || !host.services->selection) {
        record("avatar.spawn: not available in this session");
        return QString();
    }
    if (!requireProject()) return QString();

    static const QStringList known = { "position", "parent" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            record(QStringLiteral("avatar.spawn: unknown option '%1' (known: %2)")
                       .arg(it.key(), known.join(", ")));
            return QString();
        }
    }

    const auto assetRow = host.db->fetchAsset(assetGuid);
    if (assetRow.guid.isEmpty() || assetRow.type != static_cast<int>(ModelTypes::Object)) {
        record(QStringLiteral("avatar.spawn: '%1' is not an object asset").arg(assetGuid));
        return QString();
    }

    auto scene = host.services->sceneEdit->scene();
    if (!scene) { record("avatar.spawn: no scene is open"); return QString(); }

    iris::SceneNodePtr parent;
    if (options.contains("parent")) {
        parent = scriptmod::findNodeByGuid(scene->getRootNode(), options.value("parent").toString());
        if (!parent) {
            record(QStringLiteral("avatar.spawn: no node with id '%1' to parent under")
                       .arg(options.value("parent").toString()));
            return QString();
        }
    }

    // ONE instantiation route (ASSET_PIPELINE_SPEC): the same call
    // assets.addToScene makes. An avatar is not a second kind of import.
    const bool hasPosition = options.contains("position");
    host.services->selection->select(iris::SceneNodePtr());
    host.services->sceneEdit->addMaterialMesh(QString(), hasPosition,
                                              scriptmod::vecFromJs(options.value("position")),
                                              assetGuid, assetRow.name);
    auto node = host.services->selection->selected();
    if (!node) {
        record("avatar.spawn: the asset could not be instantiated");
        return QString();
    }
    // Reparenting keeps the world pose (addChild's default), so a
    // `parent` option cannot silently teleport the character.
    if (parent) parent->addChild(node);

    // The COMPONENT goes on the wrapper the import produced — the node the
    // movement writes a transform to — and the SOCKETS go on the rigged mesh
    // inside it, which may be the same node or several levels down.
    auto movement = iris::AvatarMovementPtr(new iris::AvatarMovement());
    node->setAvatarComponent(movement);
    movement->fitCapsuleToNode(node);
    // EVERY skinned piece, not the first one. A Mixamo export is three meshes
    // bound to subsets of one skeleton, and the first piece depth-first is the
    // limbs — no Head, no Shoulder — so a first-piece-only install left a
    // spawned character with zero sockets and no error (and the third-person
    // camera with no shoulder to take its height from).
    avatar::sockets::installBuiltInsInSubtree(node);

    // THE LOCOMOTION STATE MACHINE (§7, Stage 4), installed at the same moment
    // and on the same node. `refreshClips` reads the character's clips, runs
    // the tolerant role matcher over their names and builds the shipped
    // "Biped Locomotion" default from whatever bound — so a spawned character
    // is already driving a state machine with ZERO authoring, which is L4's
    // whole "hide the crud" requirement. A character whose clips bind no role
    // gets an EMPTY asset and idles at bind pose: a degradation, not an error.
    auto loco = iris::AvatarLocomotionPtr(new iris::AvatarLocomotion());
    node->setLocomotionComponent(loco);
    loco->refreshClips(node, movement->params().walkSpeed, movement->params().runSpeed);

    // R6, ANSWERED: an avatar spawned WHILE PLAYING registers with the running
    // world on the spot rather than being refused. Refusing would have meant a
    // script could not spawn a character into a running game at all, and the
    // registration is one call because the component owns nothing in the world.
    if (auto env = scene->getPhysicsEnvironment())
        if (env->isSimulating()) env->addAvatarToWorld(node);

    if (host.services->undo) {
        auto weak = node.toWeakRef();
        host.services->undo->push(new NodeEditCommand(
            QStringLiteral("avatar component"),
            [weak, movement, loco]() {
                if (auto n = weak.toStrongRef()) {
                    n->setAvatarComponent(movement);
                    n->setLocomotionComponent(loco);
                }
            },
            [weak]() {
                if (auto n = weak.toStrongRef()) {
                    n->setAvatarComponent(iris::AvatarMovementPtr());
                    n->setLocomotionComponent(iris::AvatarLocomotionPtr());
                }
            }));
    }
    return node->getGUID();
}

QVariantMap AvatarApi::movement(const QString &nodeId)
{
    QVariantMap out;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) { record("avatar.movement: no scene is open"); return out; }
    auto node = scriptmod::findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) {
        record(QStringLiteral("avatar.movement: no node with id '%1'").arg(nodeId));
        return out;
    }
    if (!node->hasAvatarComponent()) return out;   // not an avatar: empty, not an error
    return paramsToJs(node->avatar()->params());
}

QVariantMap AvatarApi::setMovement(const QString &nodeId, const QVariantMap &values)
{
    QVariantMap out;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) { record("avatar.setMovement: no scene is open"); return out; }
    auto node = scriptmod::findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) {
        record(QStringLiteral("avatar.setMovement: no node with id '%1'").arg(nodeId));
        return out;
    }
    if (!node->hasAvatarComponent()) {
        record(QStringLiteral("avatar.setMovement: '%1' carries no avatar component "
                              "(avatar.spawn installs one)").arg(node->getName()));
        return out;
    }

    const QVariantMap in = scriptmod::normalizeJs(values).toMap();
    for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
        if (!knobNames().contains(it.key())) {
            record(QStringLiteral("avatar.setMovement: unknown knob '%1' (known: %2)")
                       .arg(it.key(), knobNames().join(", ")));
            return out;
        }
    }

    auto *movement = node->avatar();
    const iris::AvatarMovementParams before = movement->params();
    iris::AvatarMovementParams p = before;

    auto f = [&in](const char *key, float fallback) {
        return in.contains(QLatin1String(key)) ? in.value(QLatin1String(key)).toFloat() : fallback;
    };
    p.walkSpeed = f("walkSpeed", p.walkSpeed);
    p.runSpeed = f("runSpeed", p.runSpeed);
    p.maxAcceleration = f("maxAcceleration", p.maxAcceleration);
    p.brakingDeceleration = f("brakingDeceleration", p.brakingDeceleration);
    p.groundFriction = f("groundFriction", p.groundFriction);
    p.jumpVelocity = f("jumpVelocity", p.jumpVelocity);
    if (in.contains("jumpCount")) p.jumpCount = in.value("jumpCount").toInt();
    p.coyoteTime = f("coyoteTime", p.coyoteTime);
    p.jumpReArm = f("jumpReArm", p.jumpReArm);
    p.airControl = f("airControl", p.airControl);
    p.gravityScale = f("gravityScale", p.gravityScale);
    p.maxStepHeight = f("maxStepHeight", p.maxStepHeight);
    p.walkableFloorAngle = f("walkableFloorAngle", p.walkableFloorAngle);
    if (in.contains("orientRotationToMovement"))
        p.orientRotationToMovement = in.value("orientRotationToMovement").toBool();
    p.rotationRate = f("rotationRate", p.rotationRate);
    // An EXPLICIT dimension is a decision: it clears the auto-fit, or the next
    // spawn/load would silently derive over it. An explicit `capsuleAuto` is
    // applied AFTER, so a caller that asks for both dimensions and auto in one
    // call gets auto — the later, more specific statement of intent wins.
    if (in.contains("capsuleRadius")) { p.capsuleRadius = in.value("capsuleRadius").toFloat(); p.capsuleAuto = false; }
    if (in.contains("capsuleHeight")) { p.capsuleHeight = in.value("capsuleHeight").toFloat(); p.capsuleAuto = false; }
    if (in.contains("capsuleAuto")) p.capsuleAuto = in.value("capsuleAuto").toBool();

    movement->setParams(p);
    const iris::AvatarMovementParams after = movement->params();
    // THE BLEND SPACE'S SAMPLE POSITIONS ARE walkSpeed AND runSpeed (§7.2), so
    // a knob edit has to regenerate the generated default asset or the space
    // keeps bracketing against the speeds the character no longer has. An
    // AUTHORED asset is left alone — refreshDefaultAsset is a no-op for it —
    // and a running character keeps its state rather than being thrown back to
    // entry by a slider drag.
    if (auto *loco = node->locomotion()) loco->refreshDefaultAsset(after.walkSpeed, after.runSpeed);

    if (host.services && host.services->undo) {
        auto weak = node.toWeakRef();
        host.services->undo->push(new NodeEditCommand(
            QStringLiteral("avatar movement"),
            [weak, after]() { if (auto n = weak.toStrongRef()) if (auto *m = n->avatar()) m->setParams(after); },
            [weak, before]() { if (auto n = weak.toStrongRef()) if (auto *m = n->avatar()) m->setParams(before); }));
    }
    return paramsToJs(after);
}
// The scripted input producer (AVATAR_LOCOMOTION_SPEC §8.2). It lives on the
// `avatar` module rather than on `input` because it drives a character, not a
// binding: `input.*` is the MAP (what a key means), this is the STATE (what
// the player is doing right now). Stage 3's possession decides which avatar
// the state reaches; until then it is the state itself that is observable.
QVariantMap AvatarApi::input(const QVariantMap &params)
{
    auto &sys = iris::InputSystem::instance();
    if (params.contains(QStringLiteral("move"))) {
        const QVariantMap m = params.value(QStringLiteral("move")).toMap();
        sys.setMove(float(m.value(QStringLiteral("x")).toDouble()),
                    float(m.value(QStringLiteral("y")).toDouble()));
    }
    if (params.contains(QStringLiteral("look"))) {
        const QVariantMap m = params.value(QStringLiteral("look")).toMap();
        sys.setLook(float(m.value(QStringLiteral("x")).toDouble()),
                    float(m.value(QStringLiteral("y")).toDouble()));
    }
    if (params.contains(QStringLiteral("sprint")))
        sys.setSprint(params.value(QStringLiteral("sprint")).toBool());
    // A one-shot LATCH: true arms it, false is a NO-OP. Clearing belongs to the
    // consumer (the movement component's step), so a script that passes
    // {jump:false} on the frame after {jump:true} cannot swallow the jump.
    if (params.value(QStringLiteral("jump")).toBool())
        sys.requestJump();

    const iris::InputState &s = sys.state();
    return QVariantMap{
        { "move",   QVariantMap{ { "x", double(s.move.x) }, { "y", double(s.move.y) } } },
        { "look",   QVariantMap{ { "x", double(s.look.x) }, { "y", double(s.look.y) } } },
        { "jump",   s.jump },
        { "sprint", s.sprint },
    };
}

// ---------------------------------------------------------------------------
// POSSESSION (AVATAR_LOCOMOTION_SPEC §8.4, Stage 3)
//
// Thin by design: the slot, the routing and the follow arm all live on
// iris::AvatarPossession, which the scene owns — because every property these
// verbs expose has to be assertable in a headless document run with no window,
// no engine and no display, and because a page switch must not be able to
// destroy who you were driving.

iris::AvatarPossession *AvatarApi::possessionOrFail(const char *verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) {
        record(QStringLiteral("%1: no scene is open").arg(QLatin1String(verb)));
        return nullptr;
    }
    return scene->getPossession();
}

bool AvatarApi::possess(const QString &nodeId)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) return record("avatar.possess: no scene is open");
    auto node = scriptmod::findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) return record(QStringLiteral("avatar.possess: no node with id '%1'").arg(nodeId));
    if (!node->hasAvatarComponent())
        return record(QStringLiteral("avatar.possess: '%1' carries no avatar component "
                                     "(avatar.spawn installs one; avatar.list lists them)")
                          .arg(node->getName()));
    // NOT UNDOABLE, deliberately: possession is play state, the same class as
    // the play flag itself. An undo stack entry for "you were driving the other
    // character" would be a document edit that no file ever carries.
    return scene->getPossession()->possess(node);
}

bool AvatarApi::unpossess()
{
    auto *possession = possessionOrFail("avatar.unpossess");
    return possession ? possession->unpossess() : false;
}

QVariant AvatarApi::possessed()
{
    auto *possession = possessionOrFail("avatar.possessed");
    if (!possession) return QVariant();
    const QString guid = possession->possessedGuid();
    return guid.isEmpty() ? QVariant() : QVariant(guid);
}

QVariantList AvatarApi::list()
{
    QVariantList out;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) { record("avatar.list: no scene is open"); return out; }
    const QString possessedGuid = scene->getPossession()->possessedGuid();

    // DOCUMENT ORDER, depth-first — byte for byte the walk auto-possess and
    // Environment's avatar registration use, so list()[0] really is the avatar
    // playMode('third-person') takes over.
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &node) {
        const int kids = node->childCount();
        for (int i = 0; i < kids; ++i) {
            iris::SceneNode *raw = node->childAt(i);
            if (!raw) continue;
            auto child = raw->sharedFromThis();
            if (child->hasAvatarComponent()) {
                const iris::AvatarLocomotionState &s = child->avatar()->state();
                out.append(QVariantMap{
                    { "id", child->getGUID() },
                    { "name", child->getName() },
                    { "possessed", child->getGUID() == possessedGuid },
                    { "speed", double(s.speed) },
                    { "grounded", s.grounded },
                    { "mode", s.mode == iris::AvatarMovementMode::Walking
                                  ? QStringLiteral("walking") : QStringLiteral("falling") },
                    { "state", child->locomotion() ? child->locomotion()->currentState()
                                                   : QString() },
                });
            }
            walk(child);
        }
    };
    walk(scene->getRootNode());
    return out;
}

QVariantMap AvatarApi::followCamera(const QVariantMap &values)
{
    QVariantMap out;
    auto *possession = possessionOrFail("avatar.followCamera");
    if (!possession) return out;

    static const QStringList known = { "armLength", "heightOffset", "lateralOffset",
                                       "yaw", "pitch" };
    const QVariantMap in = scriptmod::normalizeJs(values).toMap();
    for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            record(QStringLiteral("avatar.followCamera: unknown field '%1' (known: %2)")
                       .arg(it.key(), known.join(", ")));
            return out;
        }
    }

    iris::FollowCameraParams p = possession->follow();
    auto f = [&in](const char *key, float fallback) {
        return in.contains(QLatin1String(key)) ? in.value(QLatin1String(key)).toFloat() : fallback;
    };
    p.armLength = f("armLength", p.armLength);
    p.heightOffset = f("heightOffset", p.heightOffset);
    p.lateralOffset = f("lateralOffset", p.lateralOffset);
    possession->setFollow(p);
    if (in.contains(QStringLiteral("yaw"))) possession->setYaw(in.value("yaw").toFloat());
    if (in.contains(QStringLiteral("pitch"))) possession->setPitch(in.value("pitch").toFloat());
    // The angles moved, so the arm has: re-derive before reporting, or a caller
    // that yaws and then reads `position` gets the pose from before its write.
    possession->updateFollowCamera();

    const iris::FollowCameraParams &r = possession->follow();
    const iris::Vec3 pos = possession->cameraPosition();
    const iris::Quat rot = possession->cameraRotation();
    out["armLength"] = double(r.armLength);
    out["heightOffset"] = double(r.heightOffset);
    out["lateralOffset"] = double(r.lateralOffset);
    out["yaw"] = double(possession->yaw());
    out["pitch"] = double(possession->pitch());
    out["position"] = QVariantMap{ { "x", double(pos.x()) }, { "y", double(pos.y()) },
                                   { "z", double(pos.z()) } };
    out["rotation"] = QVariantMap{ { "x", double(rot.x()) }, { "y", double(rot.y()) },
                                   { "z", double(rot.z()) }, { "w", double(rot.scalar()) } };
    return out;
}

// ---------------------------------------------------------------------------
// THE STATE MACHINE (AVATAR_LOCOMOTION_SPEC §7, Stage 4)
//
// Thin, for the same reason possession's verbs are thin: everything these
// report has to be assertable in a headless document run with no window, no
// engine and no display, so the machine itself lives on iris::AvatarLocomotion
// and these verbs are a JSON skin over it. The asset's JSON grammar is the
// DOCUMENT's (iris::locomotionAssetToJson / FromJson) — the same code the scene
// writer and reader use — so a scene file and this verb cannot drift apart.

iris::AvatarLocomotion *AvatarApi::locomotionOrFail(const char *verb, const QString &nodeId,
                                                    iris::SceneNodePtr *nodeOut)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) {
        record(QStringLiteral("%1: no scene is open").arg(QLatin1String(verb)));
        return nullptr;
    }
    auto node = scriptmod::findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) {
        record(QStringLiteral("%1: no node with id '%2'").arg(QLatin1String(verb), nodeId));
        return nullptr;
    }
    if (nodeOut) *nodeOut = node;
    return node->locomotion();
}

QVariantMap AvatarApi::locomotionState(const QString &nodeId)
{
    QVariantMap out;
    iris::SceneNodePtr node;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) { record("avatar.locomotionState: no scene is open"); return out; }
    node = scriptmod::findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) {
        record(QStringLiteral("avatar.locomotionState: no node with id '%1'").arg(nodeId));
        return out;
    }
    if (!node->hasAvatarComponent()) return out;   // not an avatar: empty, not an error

    // THE §5 CONTRACT, straight off the movement component — the same struct
    // the state machine read this step, never a re-derivation.
    const iris::AvatarLocomotionState &s = node->avatar()->state();
    out["speed"] = double(s.speed);
    out["moveInput"] = QVariantMap{ { "x", double(s.moveInput.x()) },
                                    { "y", double(s.moveInput.y()) },
                                    { "z", double(s.moveInput.z()) } };
    out["grounded"] = s.grounded;
    out["verticalVelocity"] = double(s.verticalVelocity);
    out["jumpRequested"] = s.jumpRequested;
    out["mode"] = s.mode == iris::AvatarMovementMode::Walking ? QStringLiteral("walking")
                                                              : QStringLiteral("falling");

    QVariantList weights;
    if (auto *loco = node->locomotion()) {
        out["state"] = loco->currentState();
        out["previousState"] = loco->previousState();
        out["transitioning"] = loco->isTransitioning();
        out["transitionProgress"] = double(loco->transitionProgress());
        out["transitionDuration"] = double(loco->transitionDuration());
        out["phase"] = double(loco->statePhase());
        for (const auto &w : loco->weights())
            weights.append(QVariantMap{ { "clip", w.clip },
                                        { "weight", double(w.weight) },
                                        { "time", double(w.time) },
                                        { "looping", w.looping } });
    } else {
        out["state"] = QString();
        out["previousState"] = QString();
        out["transitioning"] = false;
        out["transitionProgress"] = 1.0;
        out["transitionDuration"] = 0.0;
        out["phase"] = 0.0;
    }
    out["weights"] = weights;
    return out;
}

QVariantMap AvatarApi::locomotionAsset(const QString &nodeId)
{
    QVariantMap out;
    auto *loco = locomotionOrFail("avatar.locomotionAsset", nodeId);
    if (!loco) return out;
    out = iris::locomotionAssetToJson(loco->asset()).toVariantMap();
    out["default"] = loco->usesDefaultAsset();
    return out;
}

bool AvatarApi::setLocomotionAsset(const QString &nodeId, const QVariantMap &values)
{
    iris::SceneNodePtr node;
    auto *loco = locomotionOrFail("avatar.setLocomotionAsset", nodeId, &node);
    if (!loco) {
        if (node)
            return record(QStringLiteral("avatar.setLocomotionAsset: '%1' carries no locomotion "
                                         "component (avatar.spawn installs one)")
                              .arg(node->getName()));
        return false;
    }

    // ONE grammar (the document's), so a refusal here reads exactly like a
    // refusal from a hand-edited scene file, and gate S6's round trip covers
    // both paths because they ARE one path.
    const QJsonObject obj =
        QJsonObject::fromVariantMap(scriptmod::normalizeJs(values).toMap());
    iris::LocomotionAsset asset;
    QString err;
    if (!iris::locomotionAssetFromJson(obj, asset, &err))
        return record(QStringLiteral("avatar.setLocomotionAsset: %1").arg(err));

    const iris::LocomotionAsset before = loco->asset();
    const bool wasDefault = loco->usesDefaultAsset();
    if (!loco->setAsset(asset, &err))
        return record(QStringLiteral("avatar.setLocomotionAsset: %1").arg(err));

    if (host.services && host.services->undo) {
        auto weak = node.toWeakRef();
        host.services->undo->push(new NodeEditCommand(
            QStringLiteral("locomotion asset"),
            [weak, asset]() {
                if (auto n = weak.toStrongRef())
                    if (auto *l = n->locomotion()) { QString e; l->setAsset(asset, &e); }
            },
            [weak, before, wasDefault]() {
                if (auto n = weak.toStrongRef())
                    if (auto *l = n->locomotion()) {
                        // The FLAG travels with the asset: undoing back to the
                        // generated default has to restore "this is the
                        // default" too, or the next role bind would refuse to
                        // regenerate what it is looking at.
                        QString e;
                        l->restoreAsset(before, wasDefault, &e);
                    }
            }));
    }
    return true;
}

QVariantMap AvatarApi::clipRoles(const QString &nodeId)
{
    QVariantMap out;
    iris::SceneNodePtr node;
    auto *loco = locomotionOrFail("avatar.clipRoles", nodeId, &node);
    if (!loco) return out;

    // The table is refreshed first: a clip loaded onto the character after it
    // was spawned must show up here, and re-matching is what the physics tick
    // would have done on the next step anyway.
    const iris::AvatarMovementParams p = node->hasAvatarComponent()
                                             ? node->avatar()->params()
                                             : iris::AvatarMovementParams();
    loco->refreshClips(node, p.walkSpeed, p.runSpeed);

    const iris::ClipRoles &r = loco->roles();
    out["idle"] = r.get(iris::ClipRole::Idle);
    out["walk"] = r.get(iris::ClipRole::Walk);
    out["run"] = r.get(iris::ClipRole::Run);
    out["jumpStart"] = r.get(iris::ClipRole::JumpStart);
    out["fallLoop"] = r.get(iris::ClipRole::FallLoop);
    out["land"] = r.get(iris::ClipRole::Land);

    QVariantList clips;
    const QMap<QString, float> &lengths = loco->clipLengths();
    for (auto it = lengths.constBegin(); it != lengths.constEnd(); ++it)
        clips.append(QVariantMap{ { "name", it.key() }, { "length", double(it.value()) } });
    out["clips"] = clips;
    return out;
}

bool AvatarApi::setClipRole(const QString &nodeId, const QString &role, const QString &clipName)
{
    iris::SceneNodePtr node;
    auto *loco = locomotionOrFail("avatar.setClipRole", nodeId, &node);
    if (!loco) {
        if (node)
            return record(QStringLiteral("avatar.setClipRole: '%1' carries no locomotion "
                                         "component (avatar.spawn installs one)")
                              .arg(node->getName()));
        return false;
    }
    iris::ClipRole r;
    if (!iris::clipRoleFromName(role, r))
        return record(QStringLiteral("avatar.setClipRole: '%1' is not a role (known: idle, "
                                     "walk, run, jumpStart, fallLoop, land)").arg(role));

    const QString before = loco->roles().get(r);
    loco->setClipRole(r, clipName);
    if (host.services && host.services->undo) {
        auto weak = node.toWeakRef();
        host.services->undo->push(new NodeEditCommand(
            QStringLiteral("clip role"),
            [weak, r, clipName]() {
                if (auto n = weak.toStrongRef())
                    if (auto *l = n->locomotion()) l->setClipRole(r, clipName);
            },
            [weak, r, before]() {
                if (auto n = weak.toStrongRef())
                    if (auto *l = n->locomotion()) l->setClipRole(r, before);
            }));
    }
    return true;
}
