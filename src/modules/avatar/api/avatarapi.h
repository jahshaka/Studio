/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARAPI_H
#define AVATARAPI_H

// avatar.* — the Avatar module's verbs (AVATAR_MODULE_SPEC §0.10).
//
// Every verb but `snapshot` drives AvatarPreviewModel, which has no engine in
// it, so the whole surface runs under QT_QPA_PLATFORM=offscreen with no engine
// at all — that is what makes the module API-first-testable. `snapshot` needs
// the bridge's offscreen render, injected as a delegate by AvatarModule when
// the engine is up.
//
// Part 0 is a VIEWER: nothing here touches the library, the database, the
// project or the editor scene's clock. avatar.spawn/list/info/setClipRole are
// Part 1's.

#include <QHash>
#include <QImage>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "irisgl/irisglfwd.h"
#include "scripting/apimodule.h"

#include "modules/avatar/avatarpreviewmodel.h"   // HeightNormalization (a member)
#include "services/avatarassets.h"                // the open-asset scope + definition

namespace avatar { class AvatarPreviewModel; }
namespace iris { class AvatarPossession; class AvatarLocomotion; }
class ImportBatchRunner;
class QTimer;
template <typename T> class QFutureWatcher;

class AvatarApi : public ApiModule
{
    Q_OBJECT
public:
    AvatarApi(ScriptHost &host, avatar::AvatarPreviewModel *model, QObject *parent = nullptr);

    QString jsName() const override { return QStringLiteral("avatar"); }
    QVector<VerbInfo> verbs() const override;

    /// Offscreen render of the module's preview scene (bridge-side). Unset in
    /// headless hosts: avatar.snapshot then fails cleanly instead of crashing.
    using SnapshotFn = std::function<QImage(int, int)>;
    void setSnapshotDelegate(SnapshotFn fn) { mSnapshot = std::move(fn); }
    /// Makes the engine evaluate the current clip time before a pose is read.
    /// Injected by AvatarModule when the engine is up; unset in headless hosts,
    /// where `bones()` reports the rig's shape at its REST pose (documented in
    /// the verb, and why `bones` is Needs::Engine).
    void setPoseResolver(std::function<void()> fn) { mResolvePose = std::move(fn); }
    /// The module persists the space-mode choice; the verb reports through this.
    void setPersistModeDelegate(std::function<void(const char *)> fn) { mPersistMode = std::move(fn); }
    /// Called after any verb that changes what the page shows, so the widgets
    /// follow scripted state (the materials module's selection-delegate shape).
    void setChangedDelegate(std::function<void()> fn) { mChanged = std::move(fn); }
    /// Called ONLY when the subject itself changed (load/clear) — that is when
    /// the camera re-frames. Re-framing on every setTime would fight the orbit.
    void setSubjectDelegate(std::function<void()> fn) { mSubjectChanged = std::move(fn); }

    /// Explicit height override for the preview subject (metres); <= 0 re-runs
    /// the automatic rule. Returns the same map `preview()` does.
    Q_INVOKABLE QVariant setCharacterHeight(double metres);
    Q_INVOKABLE QVariant loadAnimation(const QString &pathOrAssetGuid,
                                       const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool clearPreview();
    Q_INVOKABLE bool setRootMotion(bool on);
    Q_INVOKABLE QVariant spaceMode(const QVariant &mode = QVariant());

    /// The message of the last verb failure, for the widgets. ApiModule::fail
    /// throws into the JS engine, which a button click has no access to — the
    /// page still has to be able to show a rig-mismatch refusal to the user.
    QString lastError() const { return mLastError; }
    Q_INVOKABLE QVariant preview();
    Q_INVOKABLE bool setMeshVisible(bool on);
    Q_INVOKABLE bool setSkeletonVisible(bool on);
    Q_INVOKABLE bool setRigVisible(bool on);
    Q_INVOKABLE QVariantList clips();
    /// Every ModelTypes::Animation row in the library, with the rig question
    /// answered against the loaded character (see the verb's help).
    Q_INVOKABLE QVariantList animations();
    Q_INVOKABLE bool playClip(const QString &name = QString());
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool setClip(const QString &name);
    Q_INVOKABLE bool setLooping(bool on);
    Q_INVOKABLE bool setTime(double seconds);
    Q_INVOKABLE double time();
    Q_INVOKABLE QVariantList bones();
    /// The module's built-in head/shoulder sockets, on an editor-scene node
    /// (CAMERAS_SPEC D9). The only verb here that leaves the preview document.
    Q_INVOKABLE QVariantList addSockets(const QString &nodeId);

    // ---- AVATAR_LOCOMOTION_SPEC Stage 2 (§10) ----------------------------
    //
    // Editor-SCENE verbs, unlike everything above them: these three address a
    // node in the open scene by guid, exactly as `addSockets` does.
    Q_INVOKABLE QString spawn(const QString &assetGuid,
                              const QVariantMap &options = QVariantMap());
    /// Loads an animation clip onto a SCENE avatar (verb-coverage audit F5).
    /// The Mixamo workflow, for the scene rather than the preview page: one
    /// character asset, then one file per animation.
    Q_INVOKABLE QVariantMap loadClip(const QString &nodeId, const QString &pathOrAssetGuid,
                                     const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap movement(const QString &nodeId);
    Q_INVOKABLE QVariantMap setMovement(const QString &nodeId, const QVariantMap &values);
    Q_INVOKABLE QVariantMap snapshot(const QString &path, int width = 256, int height = 256,
                                     const QVariantList &probes = QVariantList());

    // ---- locomotion input (AVATAR_LOCOMOTION_SPEC §8.2, Stage 1) ----------
    // The SCRIPTED input producer. It writes the same iris::InputState the
    // keyboard producer writes, which is the whole reason every locomotion
    // test in this program can run headless with no synthetic key events.
    Q_INVOKABLE QVariantMap input(const QVariantMap &params = QVariantMap());

    // ---- possession (AVATAR_LOCOMOTION_SPEC §8.4, Stage 3) ----------------
    // One slot per scene, shaped like scene.setActiveCamera. Runtime only:
    // nothing here is serialized and nothing here is undoable — possession is
    // play state, not a document edit.
    Q_INVOKABLE bool possess(const QString &nodeId);
    Q_INVOKABLE bool unpossess();
    Q_INVOKABLE QVariant possessed();
    Q_INVOKABLE QVariantList list();
    Q_INVOKABLE QVariantMap followCamera(const QVariantMap &values = QVariantMap());

    // ---- the state machine (AVATAR_LOCOMOTION_SPEC §7, Stage 4) -----------
    // The §5 parameter contract plus what the state machine did with it, the
    // asset as data, and the role bindings. `locomotionState` is the assertion
    // surface every gate in this program reads.
    Q_INVOKABLE QVariantMap locomotionState(const QString &nodeId);
    Q_INVOKABLE QVariantMap locomotionAsset(const QString &nodeId);
    Q_INVOKABLE bool setLocomotionAsset(const QString &nodeId, const QVariantMap &values);
    Q_INVOKABLE QVariantMap clipRoles(const QString &nodeId);
    Q_INVOKABLE bool setClipRole(const QString &nodeId, const QString &role,
                                 const QString &clipName);

    // ---- AVATAR ASSETS (AVATAR_ASSET_SPEC §6) -----------------------------
    //
    // The module edits an ASSET — a library one or the open project's version
    // of one — and says which. Scenes hold INSTANCES of the project's version.
    // Everything here is document/DB work: no engine, no display.
    Q_INVOKABLE QVariantList library(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString createAsset(const QString &objectGuid,
                                    const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap importAvatar(const QString &path,
                                         const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap open(const QString &guid, const QVariantMap &options = QVariantMap());

    // ---- AV1: the module must not freeze the app --------------------------
    //
    // `importAvatar` and `open` both took seconds ON THE UI THREAD (measured on
    // the owner's Jennifer.fbx: 9332 ms to import, 1541 ms to switch), which is
    // an app that looks crashed. Both take `{async: true}`, which starts the
    // work — the pipeline's own ImportBatchRunner for the import, the preview
    // parse on a QtConcurrent worker for the switch — and RETURNS IMMEDIATELY,
    // exactly as `editor.importAssets` does for the Assets page. `progress()`
    // is what a script (and the page's ProgressDialog) watches.
    Q_INVOKABLE QVariantMap progress();
    Q_INVOKABLE bool cancelImport();
    /// The module is going away (AvatarModule::shutdown): stop and join every
    /// background job, then forget the model. Without it a worker's completion
    /// would re-enter a freed AvatarPreviewModel.
    void detachModel();
    /// THE QUIT IS STARTING (StudioModule::abortBackgroundWork, CLEANUP-1 item
    /// 2). Flush the coalesced definition write, move the load epoch so no
    /// in-flight parse can apply anything, and tell the import runner to stop —
    /// then RETURN, without joining: the shell's own bounded pool wait is what
    /// joins, a few lines later, and it is the wait that used to time out at
    /// three seconds while this module had not yet been asked to stop at all.
    /// Idempotent; detachModel() still does the ordered teardown afterwards.
    void abortBackgroundWork();
    Q_INVOKABLE QVariantMap asset();
    Q_INVOKABLE QVariantMap save();
    Q_INVOKABLE QVariantMap saveToLibrary(const QString &guid = QString());
    Q_INVOKABLE bool removeClip(const QString &name);
    Q_INVOKABLE QVariantMap setClipOptions(const QString &name, const QVariantMap &values);
    Q_INVOKABLE bool setDefaultClip(const QString &name);

    // ---- LINKED INSTANCES (AVATAR_ASSET_SPEC §5.4) ------------------------
    Q_INVOKABLE QVariantList instances(const QString &assetGuid = QString());
    Q_INVOKABLE QVariantMap refreshInstances(const QString &assetGuid);

    /// The pin-change subscription's body (AssetService::onPinChanged): every
    /// instance of `assetGuid` in the open scene re-resolves. Public so the
    /// module can wire the announcement without the API knowing about services
    /// it does not own.
    void onAssetPinChanged(const QString &assetGuid);

    /// The scene-open announcement's body (StudioServices::onSceneOpened): a
    /// scene whose instances name an older definition version than the project
    /// pins re-resolves once, on load. The pin signal covers the other
    /// direction (the asset changed while the scene was open).
    void onSceneOpened();

    /// Runs `fn` with refusals RECORDED but not thrown — the wrapper every
    /// shell-side caller (a menu row, a drop, a button) uses, for the reason
    /// SilentScope documents. Read `lastError()` afterwards.
    template <typename Fn>
    auto quietly(Fn &&fn) -> decltype(fn())
    {
        SilentScope scope(this);
        mLastError.clear();
        return fn();
    }

signals:
    /// A write-through FAILED and the user can do something about it (the
    /// owner's rule: a problem the user can fix gets a visible message, not a
    /// log line). The page shows it; the edit stays in the session, dirty, and
    /// Save is the retry.
    void persistFailed(const QString &message);
    /// The page's ProgressDialog, driven by the verbs (never by the widgets):
    /// one background job at a time, started/staged/finished.
    void busyStarted(const QString &title, bool cancellable);
    void busyStage(const QString &stage, int done, int total);
    void busyFinished(bool cancelled, const QString &error);

private:
    // ---- the one background job (AV1) ------------------------------------
    struct Job
    {
        QString kind;          ///< "import" | "open" | "" (idle)
        bool running = false;
        QString file;          ///< what is being worked on (display name)
        QString stage;
        int done = 0;
        int total = 0;
        bool cancelled = false;
        QString error;
        QVariantMap result;    ///< the finished job's return value
    };
    Job mJob;
    ImportBatchRunner *mImportRunner = nullptr;
    /// The import job's tail, held while the runner works.
    struct PendingImport
    {
        AvatarAssets::Scope scope = AvatarAssets::Scope::Library;
        QString name;          ///< the {name} override
        QString objectGuid;    ///< filled by fileFinished
        QStringList warnings;
    };
    PendingImport mPendingImport;
    /// The async open's worker + what to do with its result.
    QFutureWatcher<std::shared_ptr<avatar::AvatarPreviewModel::PreparedSubject>> *mOpenWatcher
        = nullptr;
    /// Monotonic "which subject the module means to show". Bumped by every open
    /// (sync or async) and by detachModel; a parse applies its result only when
    /// the epoch it started in is still current.
    quint64 mLoadEpoch = 0;
    /// Preview parses that have COMPLETED, applied or abandoned. Reported by
    /// progress() as `parsesFinished` — an abandoned parse never returns to
    /// `running`, so this is the only thing a caller can wait on.
    quint64 mPreviewParsesFinished = 0;
    /// Starts (or chains) the two async jobs; both report through mJob.
    /// A DEFINITION EDIT IS REFUSED WHILE A JOB RUNS (lead review, AV1 round 2
    /// — a data-corruption class, not a nicety): between an async open's
    /// return and its parse landing, the module shows character A while the
    /// OPEN DEFINITION is already B, so a clip loaded in that window is
    /// matched against A's skeleton, appended to B's definition and written
    /// through into B's stored file — permanently, and invisibly once the
    /// preview swaps. Every verb that edits the definition (or picks the
    /// preview's clip) takes this first; the page greys the same controls.
    bool requireIdle(const char *verb);
    /// Abandons the preview parse in flight, if any: the epoch moves, so a
    /// result that arrives later applies nothing. EVERY open calls it — a
    /// synchronous open during an async one used to leave the preview on the
    /// old character with the definition on the new one.
    void abandonPreviewLoad();

    bool startImport(const QString &path, AvatarAssets::Scope scope, int drawerId,
                     const QString &name);
    void finishImport(bool cancelled);
    /// The definition half of `open` (cheap, UI thread). Fills mOpen and
    /// returns the preview's model path, empty when there is nothing to show.
    QString openDefinition(const QString &guid, AvatarAssets::Scope scope, QString *rowNameOut);
    void startPreviewLoad(const QString &modelPath, const QString &rowName);
    void endJob(bool cancelled, const QString &error, const QVariantMap &result = QVariantMap());

    /// loadClip's halves, shared with spawn's `clips` option.
    /// Resolves a path OR an existing asset guid to a PINNED project asset and
    /// the absolute path of its stored bytes. Empty guid on failure (message
    /// recorded).
    /// The library row already backed by this file's BYTES (content-addressed
    /// store), or empty. What keeps a clip from being imported twice (S9).
    QString assetForContent(const QString &path) const;
    QString resolveClipAsset(const char *verb, const QString &pathOrAssetGuid,
                             QString *absolutePathOut);
    /// Parses `absolutePath` for skeletal clips, scores them against the
    /// character's rig and attaches the ones that fit. Fills `out`.
    bool attachClipsFromFile(const char *verb, const iris::SceneNodePtr &character,
                             const QString &absolutePath, const QString &assetGuid,
                             const QString &nameOverride, QVariantMap &out);

    /// The node's locomotion component, or null with a message recorded.
    iris::AvatarLocomotion *locomotionOrFail(const char *verb, const QString &nodeId,
                                             iris::SceneNodePtr *nodeOut = nullptr);
    /// The open scene's possession slot, or null with a message recorded.
    iris::AvatarPossession *possessionOrFail(const char *verb);
    QVariantMap previewState() const;
    void notifyChanged();
    void notifySubjectChanged();
    /// fail(), plus a copy of the message the widgets can read back.
    bool record(const QString &message);

    /// SHELL-DRIVEN entry points do not throw. `ApiModule::fail` calls
    /// `QJSEngine::throwError`, which OUTSIDE a script evaluation leaves a
    /// PENDING exception on the engine — the next console script, MCP call or
    /// e2e line would fail with a message from a menu click it never made.
    /// A menu row and a scene open are not script calls, so they take this
    /// guard and read `lastError()` instead.
    struct SilentScope
    {
        explicit SilentScope(AvatarApi *api) : api(api) { api->mSilent = true; }
        ~SilentScope() { api->mSilent = false; }
        AvatarApi *api;
    };
    bool mSilent = false;

    // ---- what the module has OPEN (AVATAR_ASSET_SPEC §5.3) ---------------
    //
    // The module edits one avatar at a time and always knows WHICH SCOPE it
    // came from, because a save has to go back where it was opened from —
    // "asset edits go to the asset, project edits go to the project" is a
    // property of the open document, not of a preference.
    struct OpenAsset
    {
        QString guid;
        AvatarAssets::Scope scope = AvatarAssets::Scope::Library;
        QString version;                  ///< the oid it was read from
        iris::AvatarDefinition definition;
        bool dirty = false;
        bool isOpen() const { return !guid.isEmpty(); }
    };
    OpenAsset mOpen;

    /// The open definition, or a recorded refusal.
    bool requireOpenAsset(const char *verb);
    /// WRITES THE OPEN DEFINITION BACK, right now (owner 2026-09-13: "each
    /// avatar keeps track of its animations"). Every definition edit — a clip
    /// added, removed, renamed, its looping/root-motion/default changed —
    /// calls this, so switching avatars, closing the module or quitting the
    /// app can never drop it. No-op when nothing is open or nothing changed;
    /// records (never throws) when the store refuses.
    bool persistOpen(const char *verb);
    /// COALESCED (lead review, AV1 round 2): a definition write is a full CAS
    /// publish — stage, hash, ingest, pointer move or copy-on-write, dependency
    /// reconciliation, metadata, sidecar — plus a pin announcement that
    /// re-resolves every linked instance in the scene. Three clip toggles in a
    /// row were three of those. An edit marks the definition dirty and arms a
    /// short single-shot instead; the write (and its ONE announcement) happens
    /// when the user stops, and every flush point (an open, a save, shutdown)
    /// forces it immediately.
    void schedulePersist();
    /// Writes NOW when a coalesced write is pending — the flush points.
    bool flushPersist(const char *verb);
    QTimer *mPersistTimer = nullptr;
    /// Applies a definition to a spawned wrapper: clips, defaults, movement,
    /// locomotion. Shared by the linked spawn and by refreshInstances, so an
    /// instance created now and one refreshed later cannot end up different.
    bool applyDefinition(const iris::SceneNodePtr &node,
                         const iris::AvatarDefinition &definition, const QString &version,
                         QStringList *warningsOut);
    /// The project's version of `assetGuid`, loaded — or a recorded refusal.
    bool loadProjectDefinition(const char *verb, const QString &assetGuid,
                               iris::AvatarDefinition &out, QString *versionOut);
    /// D9: the linked instance's asset guid, or empty when the node is an
    /// unlinked scratch avatar. Node-level edits on a LINKED instance are
    /// redirected to the project's version of the asset.
    QString linkedAssetOf(const iris::SceneNodePtr &node) const;

    QString mLastError;
    /// What avatar.spawn's height normalization did, per spawned node guid.
    /// SESSION state on purpose: the scale itself lives on the node and is
    /// serialized, so this is only the story of how it got there.
    QHash<QString, avatar::HeightNormalization> mNormalized;

    avatar::AvatarPreviewModel *mModel = nullptr;
    SnapshotFn mSnapshot;
    std::function<void()> mResolvePose;
    std::function<void(const char *)> mPersistMode;
    std::function<void()> mChanged;
    std::function<void()> mSubjectChanged;
};

#endif // AVATARAPI_H
