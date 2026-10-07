/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/editorpage.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "bridge/enginehost.h"
#include "data/settingsmanager.h"
#include "player/engineplayerview.h"
#include "player/playerwidget.h"
#include "services/clipboardservice.h"
#include "services/playbackservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/surfaceplacement.h"
#include "shell/actionhost.h"
#include "shell/mainwindow.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "ui/dialogs/screenshotwidget.h"
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"
#include "ui/style/thememanager.h"
#include "viewport/editordata.h"
#include "viewport/enginerenderdriver.h"
#include "irisgl/document/scenegraph/scene.h"
#include "viewport/headlesseditorviewport.h"
#include "viewport/ieditorviewport.h"

EditorPage::EditorPage(QObject *parent) : QObject(parent)
{
}

void EditorPage::build(const Deps &deps)
{
    fontIcons = deps.icons;
    mShell = deps.shell;
    sceneContainer = new QWidget;
    QSizePolicy sceneContainerPolicy;
    sceneContainerPolicy.setHorizontalPolicy(QSizePolicy::Preferred);
    sceneContainerPolicy.setVerticalPolicy(QSizePolicy::Preferred);
    sceneContainerPolicy.setVerticalStretch(1);
    sceneContainer->setSizePolicy(sceneContainerPolicy);
    sceneContainer->setAcceptDrops(true);
    sceneContainer->installEventFilter(this);

    controlBar = new QWidget;
    controlBar->setObjectName(QStringLiteral("controlBar"));

    auto container = new QWidget;
    auto containerLayout = new QVBoxLayout;

    auto screenShotBtn = new QPushButton;
    screenShotBtn->setToolTip(tr("Photograph the viewport at 1920x1080 — this camera, this lens, "
                                 "and the world's own settings (global illumination, reflections, "
                                 "ambient occlusion, bloom, anti-aliasing, the looks stack and the "
                                 "exposure the view is currently at)"));
    screenShotBtn->setToolTipDuration(-1);
    screenShotBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    screenShotBtn->setIcon(QIcon(":/icons/icons8-camera-48.png"));
	screenShotBtn->setIconSize(QSize(16,17));

    wireFramesButton = new QToolButton;
    wireFramesButton->setStyleSheet(StyleSheet::ViewportMenuButton());
    wireFramesMenu = new QMenu;
	wireFramesMenu->setStyleSheet(StyleSheet::QMenuFlat());

    wireCheckAction = new QAction(QIcon(), "Light Bounds");
    wireCheckAction->setCheckable(true);
    connect(wireCheckAction, &QAction::toggled, this, [this](bool on) { sceneView->setShowLightWires(on); });
    wireFramesMenu->addAction(wireCheckAction);

    // Ground grid (EDITOR_SHORTCUTS_SPEC §3): default ON, per-scene persisted
    // beside the light-wires flag; hidden in Game View (G) and while playing.
    gridCheckAction = new QAction(QIcon(), "Ground Grid");
    gridCheckAction->setCheckable(true);
    connect(gridCheckAction, &QAction::toggled, this, [this](bool on) { if (sceneView) sceneView->setShowGrid(on); });
    wireFramesMenu->addAction(gridCheckAction);

    // Ground plane (WORLD-MODEL-1): the editor's infinite matte ground, beside
    // the grid; per-scene persisted, default OFF (EditorData::showGroundPlane).
    groundPlaneCheckAction = new QAction(QIcon(), "Ground Plane");
    groundPlaneCheckAction->setObjectName(QStringLiteral("groundPlaneCheckAction"));
    groundPlaneCheckAction->setCheckable(true);
    connect(groundPlaneCheckAction, &QAction::toggled, this, [this](bool on) {
        if (sceneView) sceneView->setShowGroundPlane(on);
    });
    wireFramesMenu->addAction(groundPlaneCheckAction);

    physicsCheckAction = new QAction(QIcon(), "Physics Debug Overlay");
    physicsCheckAction->setCheckable(true);
    connect(physicsCheckAction, &QAction::toggled, this, [this](bool on) { sceneView->setShowDebugDrawFlags(on); });
    wireFramesMenu->addAction(physicsCheckAction);

    // Selection highlight: silhouette outline by default; this shows the polygon
    // wireframe instead (engine viewport only — legacy keeps its single style).
    auto selectionWireAction = new QAction(QIcon(), "Selection Wireframe");
    selectionWireAction->setCheckable(true);
    connect(selectionWireAction, &QAction::toggled, this, [this](bool on) {
        if (sceneView) sceneView->setSelectionWireframe(on);
    });
    wireFramesMenu->addAction(selectionWireAction);

    // The engine-drawn frame-stats readout (F3). It is in this menu because
    // this is where a user looks for viewport toggles — but it is NOT one of
    // the helpers Game View hides, and it is the only row here that persists
    // (as the `show_fps` preference, shared with the Preferences checkbox).
    statsCheckAction = new QAction(QIcon(), "Frame Stats (F3)");
    statsCheckAction->setCheckable(true);
    statsCheckAction->setChecked(
        SettingsManager::getDefaultManager()->get(settingkeys::showFps));
    connect(statsCheckAction, &QAction::toggled, this,
            [this](bool on) { setShowFrameStats(on); });
    wireFramesMenu->addAction(statsCheckAction);

    // THE ATOM VIEW (D0-ATOM-VIEW): the visibility buffer in false colour. The
    // rows call the scene's setAtomView — the path world.setAtomView takes — and
    // re-read it whenever the menu opens, so a script's change shows here too.
    {
        QMenu *atomMenu = wireFramesMenu->addMenu(tr("Atom View"));
        auto *atomGroup = new QActionGroup(atomMenu);
        atomGroup->setExclusive(true);
        const QStringList atomLabels = { tr("Off"), tr("Triangles"), tr("Levels"), tr("Buckets"),
                                         tr("Objects") };
        for (int mode = 0; mode < atomLabels.size(); ++mode) {
            QAction *action = atomMenu->addAction(atomLabels[mode]);
            action->setCheckable(true);
            action->setChecked(mode == 0);
            atomGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, mode]() { setAtomViewMode(mode); });
            atomViewActions.push_back(action);
        }
        // ...and DISABLES the painting rows where nothing could paint (the Low
        // tier's passthrough viewport, the split shut) — the verb refuses there too.
        // Off stays enabled: a view left on can always be switched off.
        connect(atomMenu, &QMenu::aboutToShow, this, [this]() {
            const int mode = atomViewMode();
            jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
            const bool paintable = es && es->atomViewPaintable();
            for (int i = 0; i < atomViewActions.size(); ++i) {
                atomViewActions[i]->setChecked(i == mode);
                atomViewActions[i]->setEnabled(i == 0 || paintable);
            }
        });
    }

    // THE PHOTON VIEW (PHOTON-VIEW-1): the lighting's debug pictures, beside the
    // Atom view and in its shape — the rows call the scene's setPhotonView behind
    // its refusal (world.setPhotonView's path), re-read on open, and a row that
    // cannot paint here is disabled.
    {
        QMenu *photonMenu = wireFramesMenu->addMenu(tr("Photon View"));
        auto *photonGroup = new QActionGroup(photonMenu);
        photonGroup->setExclusive(true);
        const QStringList photonLabels = { tr("Off"), tr("Voxels"), tr("Probes"), tr("Cards"),
                                           tr("Screen Probes"), tr("Diffuse GI Only"),
                                           tr("Reflections Only"), tr("Ray Hits") };
        for (int mode = 0; mode < photonLabels.size(); ++mode) {
            QAction *action = photonMenu->addAction(photonLabels[mode]);
            action->setCheckable(true);
            action->setChecked(mode == 0);
            photonGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, mode]() { setPhotonViewMode(mode); });
            photonViewActions.push_back(action);
        }
        connect(photonMenu, &QMenu::aboutToShow, this, [this]() {
            const int mode = photonViewMode();
            jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
            for (int i = 0; i < photonViewActions.size(); ++i) {
                photonViewActions[i]->setChecked(i == mode);
                const bool paints =
                    i == 0 || (es && es->photonViewRefusal(
                                         static_cast<jahshaka::engine::PhotonView>(i)).empty());
                photonViewActions[i]->setEnabled(paints);
            }
        });
    }

    // Qlementine: the checkable actions become Switch rows (and stay in sync
    // with their QActions); a bonus is the menu no longer closes per toggle.
    ThemeManager::switchifyMenuToggles(wireFramesMenu);

    wireFramesButton->setMenu(wireFramesMenu);
    wireFramesButton->setText("View Options ");
    wireFramesButton->setPopupMode(QToolButton::InstantPopup);

    // The projection toggle, Views ▾ and Camera ▾ — the editor camera's
    // controls, owned by the ViewController.
    const ViewController::CameraControls cameraControls = deps.cameraControls;

    connect(screenShotBtn, &QPushButton::pressed, this, &EditorPage::takeScreenshot);

    QVariantMap options;
    
    auto controlBarLayout = new QHBoxLayout;
    playSceneBtn = new QPushButton(fontIcons->icon(fa::play), "Play scene");
    playSceneBtn->setToolTip("Play all animations in the scene");
    playSceneBtn->setStyleSheet(StyleSheet::BackgroundTransparent());

    options.insert("color", QColor(52, 152, 219));
    options.insert("color-active", QColor(52, 152, 219));
	playSimBtn = new QPushButton(fontIcons->icon(fa::play, options), "Simulate physics");
	playSimBtn->setToolTip("Simulate physics only");
	playSimBtn->setStyleSheet(StyleSheet::BackgroundTransparent());

    controlBarLayout->setSpacing(8);
    controlBarLayout->addWidget(screenShotBtn);
    // THE RECORD BUTTON'S PLACE (VIDEO-REC-1): beside the photo button, a slot
    // the capture module's action goes into (shell/actionhost.h) — the shell
    // never names the module.
    if (deps.actions) {
        auto *captureBar = new QToolBar;
        captureBar->setObjectName(QStringLiteral("captureBar"));
        captureBar->setIconSize(QSize(16, 16));
        captureBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        captureBar->setMovable(false);
        captureBar->setFloatable(false);
        deps.actions->addToolbarSlot(captureBar, QStringLiteral("editor.capture"));
        controlBarLayout->addWidget(captureBar);
    }
	controlBarLayout->addWidget(cameraControls.projection);
    controlBarLayout->addWidget(wireFramesButton);
    controlBarLayout->addWidget(cameraControls.views);
    controlBarLayout->addWidget(cameraControls.cameras);
    controlBarLayout->addStretch();
    controlBarLayout->addWidget(playSceneBtn);
    controlBarLayout->addSpacing(2);
#ifdef QT_DEBUG
	controlBarLayout->addWidget(playSimBtn);
#endif // QT_DEBUG

    controlBar->setLayout(controlBarLayout);
    controlBar->setStyleSheet(StyleSheet::ControlBar());

    if (!ThemeManager::classicActive()) {
        // ONE chrome button spec across the app (shared with the desktop
        // footer, owner direction): rounded grey, consistent height,
        // horizontal text gutters — replaces the square edge-tight look.
        for (QWidget *chromeBtn :
             std::initializer_list<QWidget *>{ screenShotBtn, cameraControls.projection,
                                               wireFramesButton, cameraControls.views,
                                               cameraControls.cameras,
                                               playSceneBtn, playSimBtn })
            chromeBtn->setStyleSheet(ThemeManager::chromeButtonSheet());
    }

    mPlayerControls = new QWidget;
    mPlayerControls->setStyleSheet(StyleSheet::PlayerControlsBar());

    auto playerControlsLayout = new QHBoxLayout;

    restartBtn = new QPushButton;
    restartBtn->setCursor(Qt::PointingHandCursor);
    restartBtn->setToolTip("Restart playback");
    restartBtn->setToolTipDuration(-1);
    restartBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    ThemeRoles::setFlat(restartBtn);
    restartBtn->setIcon(QIcon(":/icons/rotate-to-right.svg"));
    restartBtn->setIconSize(QSize(16, 16));

    playBtn = new QPushButton;
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setToolTip("Play the scene");
    playBtn->setToolTipDuration(-1);
    playBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    ThemeRoles::setFlat(playBtn);
    playBtn->setIcon(QIcon(":/icons/g_play.svg"));
    playBtn->setIconSize(QSize(24, 24));

    stopBtn = new QPushButton;
    stopBtn->setCursor(Qt::PointingHandCursor);
    stopBtn->setToolTip("Stop playback");
    stopBtn->setToolTipDuration(-1);
    stopBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    ThemeRoles::setFlat(stopBtn);
    stopBtn->setIcon(QIcon(":/icons/g_stop.svg"));
    stopBtn->setIconSize(QSize(16, 16));

    playerControlsLayout->setSpacing(12);
    playerControlsLayout->setContentsMargins(6, 6, 6, 6);
    playerControlsLayout->addStretch();
    playerControlsLayout->addWidget(restartBtn);
    playerControlsLayout->addWidget(playBtn);
    playerControlsLayout->addWidget(stopBtn);
    playerControlsLayout->addStretch();

    connect(restartBtn, &QPushButton::pressed, this, [this]() {
        PlaybackService *playbackService = mServices->playback;
        playBtn->setToolTip("Pause the scene");
        playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
        playbackService->restartScene();
    });

    connect(playBtn, &QPushButton::pressed, this, [this]() {
        PlaybackService *playbackService = mServices->playback;
        if (playbackService->isPlaying()) {
            playBtn->setToolTip("Play the scene");
            playBtn->setIcon(QIcon(":/icons/g_play.svg"));
            playbackService->pauseScene();
        } else {
            playBtn->setToolTip("Pause the scene");
            playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
            playbackService->playScene();
        }
    });

    connect(stopBtn, &QPushButton::pressed, this, [this]() {
        PlaybackService *playbackService = mServices->playback;
        playBtn->setToolTip("Play the scene");
        playBtn->setIcon(QIcon(":/icons/g_play.svg"));
        playbackService->stopScene();
    });

	connect(playSimBtn, &QPushButton::pressed, this, [this]() {
		PlaybackService *playbackService = mServices->playback;
		playbackService->setSimulationRunning(!playbackService->isSimulationRunning());

        QVariantMap options;

		if (playbackService->isSimulationRunning()) {
			playbackService->startSimulation();

            playSimBtn->setText("Stop Simulation");
			playSimBtn->setToolTip("Pause physics simulation");

            options.insert("color", QColor(241, 196, 15));
            options.insert("color-active", QColor(241, 196, 15));
            playSimBtn->setIcon(fontIcons->icon(fa::stop, options));
		}
		else {
            playbackService->restartSimulation();

            playSimBtn->setText("Simulate Physics");
			playSimBtn->setToolTip("Simulate physics only");

            options.insert("color", QColor(52, 152, 219));
            options.insert("color-active", QColor(52, 152, 219));
            playSimBtn->setIcon(fontIcons->icon(fa::play, options));
		}

        if (auto sel = mServices->selection->selected()) mServices->selection->select(sel);
	});

    mPlayerControls->setLayout(playerControlsLayout);

    containerLayout->setSpacing(0);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(controlBar);
    containerLayout->addWidget(sceneContainer);
    containerLayout->addWidget(mPlayerControls);

    container->setLayout(containerLayout);

    QMainWindow *viewPort = new QMainWindow;
    mViewPort = viewPort;
    viewPort->setWindowFlags(Qt::Widget);
    viewPort->setCentralWidget(container);
    // THE RIGHT COLUMN RUNS TO THE BOTTOM (owner, 2026-09-12): the bottom-right
    // corner belongs to the right dock area, so Properties + Presets extend the
    // full height of the editor and the bottom Tray (Assets | Console,
    // Timeline) stops at the right column's edge instead of running under it.
    viewPort->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // The engine viewport is the only renderer. When the engine cannot start
    // (offscreen platform: --headless scripts, --dump-api-docs) a document-only
    // stand-in serves the document verbs; nothing renders.
    sceneView = nullptr;
    {
        auto &host = EngineHost::instance();
        QString error;
        // The engine starts even on the offscreen platform now — the document
        // scene graph IS the engine's (SPECS/SCENEGRAPH_SPEC.md D2), so a
        // --headless run needs one. What it does NOT get is an on-screen
        // viewport: nothing can present into a widget that has no native
        // window, and the document-only stand-in below is what those runs have
        // always used.
        // ASK THE ENGINE, not the platform name. A headless engine (NULL render
        // system, SPECS/SCENEGRAPH_SPEC.md §3b) can hold no View of any kind, so
        // it gets the stand-in; anything else renders. Testing for "xcb" here
        // was a Linux-shaped bug: on macOS the platform is `cocoa` and the
        // editor would have fallen back to the document-only viewport on a
        // machine whose on-screen Metal/Vulkan viewport works
        // (SPECS/MACOS_VIEWPORT_SPEC.md).
        if (host.start(error) && !host.engine()->isHeadless()) {
            sceneView = createEngineSceneViewport(host.engine(), host.driver(), viewPort);
            // The window watches the Engine (step 6 of the shutdown order checks
            // it died with the viewports) and paces the loop — PANEL-AWARE PACING
            // (fps audit F1): no more literal 16. The driver derives its interval
            // from the screen the window is on and from the persisted pacing
            // mode, fed before the driver starts.
            if (deps.engineStarted) deps.engineStarted(host.engine());
            host.driver()->start();
        } else if (!error.isEmpty()) {
            qCritical("Engine unavailable (%s): using the headless document-only viewport.",
                      qPrintable(error));
        }
    }
    if (!sceneView) sceneView = new HeadlessEditorViewport(viewPort);
    sceneView->asWidget()->setParent(viewPort);
    sceneView->asWidget()->setFocusPolicy(Qt::ClickFocus);
    sceneView->asWidget()->setFocus();
    sceneView->setShell(deps.shell ? deps.shell->view() : nullptr);
    sceneView->setDatabase(deps.db);

	// The player page: PlayerWidget gets an EnginePlayerView (a second engine
	// Scene mirroring the same document), or none in headless runs.
	mPlayerBackend = nullptr;
	if (EngineHost::instance().isRunning()) {
		auto &host = EngineHost::instance();
		mPlayerBackend = createEnginePlayerView(host.engine(), host.driver(), viewPort);
		mPlayerBackend->setEditorViewport(sceneView);
	}
	playerView = new PlayerWidget(viewPort, mPlayerBackend);

    // ONE DOOR (STUDIO-CRUD-1 item 8): the menu's grid / light-wire / stats
    // checkmarks follow the viewport's state through overlaysChanged — so
    // editor.setOverlays, the shortcuts and a scene open move them exactly as
    // a click does — and the actions' toggled() call the path the verb calls.
    connect(sceneView->events(), &EditorViewportEvents::overlaysChanged,
            this, &EditorPage::syncOverlayChecks);
    syncOverlayChecks();
	physicsCheckAction->setChecked(sceneView->getShowDebugDrawFlags());
    // The persisted readout state reaches the viewport HERE, not when the menu
    // action was built: the View Options menu is constructed before sceneView
    // exists, so its initial setChecked found nothing to switch on.
    setShowFrameStats(SettingsManager::getDefaultManager()->get(settingkeys::showFps));

    QGridLayout* layout = new QGridLayout;
    layout->addWidget(sceneView->asWidget(), 0, 0);
    // NO COVER WIDGET (owner decision D2). This grid cell used to hold a
    // second widget stacked over the viewport — ViewportCover — which, to be
    // visible over a native render window on X11, had to own a native window of
    // its own and raise() itself above the viewport's. The cover is now drawn
    // by the engine, inside the frame it was already presenting, so there is
    // nothing to add here and no stacking order to get wrong.
    layout->setContentsMargins(0, 0, 0, 0);
    sceneContainer->setLayout(layout);

    auto events = sceneView->events();
    // A DROP RESTS ON WHAT IT WAS DROPPED ON (owner, 2026-09-14). These two
    // signals are the drag-and-drop route and nothing else, so this is where
    // "the point under the cursor" becomes "the surface under the cursor";
    // assets.addToScene and the menus keep placing the pivot.
    connect(events, &EditorViewportEvents::addDroppedMesh, this, [this](QString path, bool v, iris::Vec3 pos, QString guid, QString name) {
        mServices->sceneEdit->addMaterialMesh(path, v, pos, guid, name,
                                              surfaceplacement::Placement::OnSurface);
    });

    // Straight to the service, WITH the drop point (smoke S2). The shell hop
    // this replaced (the shell's old addPrimitiveObject) forwarded one argument and
    // had exactly one caller — this lambda.
    connect(events, &EditorViewportEvents::addPrimitive, this,
            [this](QString guid, iris::Vec3 position) {
        mServices->sceneEdit->addPrimitive(guid, position, surfaceplacement::Placement::OnSurface);
    });

    connect(events, &EditorViewportEvents::addDroppedParticleSystem, this, [this](bool v, iris::Vec3 pos, QString guid, QString name) {
        mServices->sceneEdit->addAssetParticleSystem(v, pos, guid, name);
    });

    connect(events, &EditorViewportEvents::addDroppedImagePlane, this, [this](iris::Vec3 pos, QString guid) {
        mServices->sceneEdit->addImagePlane(guid, pos);
    });

    connect(events, &EditorViewportEvents::sceneNodeSelected, this,
            [this](iris::SceneNodePtr node) { mServices->selection->select(node); });

    connect(playSceneBtn, &QPushButton::clicked, this, &EditorPage::onPlaySceneButton);
}

void EditorPage::setPhysicsDebugChecked(bool on)
{
    if (physicsCheckAction) physicsCheckAction->setChecked(on);
}

void EditorPage::showPlayerPlaying()
{
    playBtn->setToolTip("Pause the scene");
    playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
}

void EditorPage::resetSimulationButton()
{
    playSimBtn->setText("Simulate Physics");
    playSimBtn->setToolTip("Simulate physics only");

    QVariantMap options;
    options.insert("color", QColor(52, 152, 219));
    options.insert("color-active", QColor(52, 152, 219));
    playSimBtn->setIcon(fontIcons->icon(fa::play, options));
}

void EditorPage::setShowFrameStats(bool on)
{
    // ONE code path for the F3 key, the View Options row, the Preferences
    // checkbox and editor.setOverlays({stats}) — and one stored value, so the
    // readout is still there after a restart (STATS_OVERLAY_SPEC §5.3). The
    // View Options checkmark follows the viewport's overlaysChanged
    // (syncOverlayChecks), never this call.
    if (sceneView) sceneView->setShowFps(on);
    SettingsManager::getDefaultManager()->set(settingkeys::showFps, on);
}

void EditorPage::setPhysicsDebugOverlay(bool on)
{
    // The action's toggled() signal calls toggleDebugDrawer, which is the one
    // path to the viewport — so setting the checkmark IS setting the overlay.
    // (A no-op setChecked emits nothing, hence the explicit fallback.)
    if (physicsCheckAction && physicsCheckAction->isChecked() != on)
        physicsCheckAction->setChecked(on);
    else
        sceneView->setShowDebugDrawFlags(on);
}

void EditorPage::syncOverlayChecks()
{
    if (!sceneView) return;
    // Not signal-blocked: the World panel's Show Grid row follows the grid
    // action's toggled(), and the round trip ends at the viewport's setter,
    // which ignores a value it already holds.
    if (gridCheckAction) gridCheckAction->setChecked(sceneView->getShowGrid());
    if (groundPlaneCheckAction) groundPlaneCheckAction->setChecked(sceneView->getShowGroundPlane());
    if (wireCheckAction) wireCheckAction->setChecked(sceneView->getShowLightWires());
    // The stats action's toggled() persists show_fps (setShowFrameStats), so it
    // is blocked: the state it follows was written by whoever moved it.
    if (statsCheckAction && statsCheckAction->isChecked() != sceneView->getShowFps()) {
        QSignalBlocker block(statsCheckAction);
        statsCheckAction->setChecked(sceneView->getShowFps());
    }
}

QVariantMap EditorPage::viewOptionChecks() const
{
    QVariantMap out;
    if (gridCheckAction) out[QStringLiteral("grid")] = gridCheckAction->isChecked();
    if (groundPlaneCheckAction)
        out[QStringLiteral("groundPlane")] = groundPlaneCheckAction->isChecked();
    if (wireCheckAction) out[QStringLiteral("lightWires")] = wireCheckAction->isChecked();
    if (statsCheckAction) out[QStringLiteral("stats")] = statsCheckAction->isChecked();
    if (physicsCheckAction) out[QStringLiteral("physicsDebug")] = physicsCheckAction->isChecked();
    return out;
}

// A BRAND-NEW SCENE STARTS AT THE DEFAULTS (owner report 2026-09-07) — the
// three overlays the open path pushes out of the saved EditorData. ONE body for
// newScene and the create run (it was two copies). The grid and light-wire
// checkmarks follow through overlaysChanged (syncOverlayChecks).
void EditorPage::resetOverlaysToDefaults()
{
    const EditorData defaults;
    sceneView->setShowGrid(defaults.showGrid);
    sceneView->setShowGroundPlane(defaults.showGroundPlane);
    sceneView->setShowLightWires(defaults.showLightWires);
    sceneView->setShowDebugDrawFlags(defaults.showDebugDrawFlags);
    if (physicsCheckAction) physicsCheckAction->setChecked(defaults.showDebugDrawFlags);
}

int EditorPage::atomViewMode() const
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    return es ? int(es->atomView()) : 0;
}

void EditorPage::setAtomViewMode(int mode)
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    if (!es || mode < 0 || mode > 4) return;
    if (mode != 0 && !es->atomViewPaintable()) return;   // world.setAtomView's refusal
    es->setAtomView(static_cast<jahshaka::engine::AtomView>(mode));
    for (int i = 0; i < atomViewActions.size(); ++i) atomViewActions[i]->setChecked(i == mode);
}

int EditorPage::photonViewMode() const
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    return es ? int(es->photonView()) : 0;
}

void EditorPage::setPhotonViewMode(int mode)
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    if (!es || mode < 0 || mode >= jahshaka::engine::kPhotonViewCount) return;
    const auto view = static_cast<jahshaka::engine::PhotonView>(mode);
    if (!es->photonViewRefusal(view).empty()) return;   // world.setPhotonView's refusal
    es->setPhotonView(view);
    for (int i = 0; i < photonViewActions.size(); ++i) photonViewActions[i]->setChecked(i == mode);
}

void EditorPage::takeScreenshot()
{
    // THE USER'S DOOR, AND IT ASKS FOR THE SCENE'S OWN PICTURE (owner,
    // 2026-09-13: "match the screenshot to the scene properly"). The grade is
    // named here rather than left to the viewport's default because the default
    // door is the THUMBNAIL grade, the selftest's, which must stay cheap. See
    // IEditorViewport::ScreenshotGrade for what each answer is a picture of.
    auto img = sceneView->takeScreenshot(1920, 1080,
                                         IEditorViewport::ScreenshotGrade::Scene);
    ScreenshotWidget screenshotWidget;
    screenshotWidget.setMaximumWidth(1280);
    screenshotWidget.setMaximumHeight(720);
    screenshotWidget.layout()->setSizeConstraint(QLayout::SetNoConstraint);
    screenshotWidget.setImage(img);
    screenshotWidget.exec();
}

void EditorPage::onPlaySceneButton()
{
	PlaybackService *playbackService = mServices->playback;
	playbackService->setSimulationRunning(!playbackService->isSimulationRunning());

    if (playbackService->isPlaying()) {
        playbackService->enterEditMode();   // chrome follows via applyEditModeUi()
		sceneView->stopPlayingScene();
    }
    else {
        playbackService->enterPlayMode();   // chrome follows via applyPlayModeUi()
		sceneView->startPlayingScene();
    }

	if (auto sel = mServices->selection->selected()) mServices->selection->select(sel);
}

void EditorPage::applyEditModeUi()
{
    playSceneBtn->setText("Play Scene");
    playSceneBtn->setToolTip("Play scene");
    QVariantMap options;
    options.insert("color", QColor(46, 204, 113));
    options.insert("color-active", QColor(46, 204, 113));
    playSceneBtn->setIcon(fontIcons->icon(fa::play, options));
}

void EditorPage::applyPlayModeUi()
{
    playSceneBtn->setEnabled(true);
    playSceneBtn->setText("Stop playing");
    playSceneBtn->setToolTip("Stop playing");

    QVariantMap options;
    options.insert("color", QColor(231, 76, 60));
    options.insert("color-active", QColor(231, 76, 60));
    playSceneBtn->setIcon(fontIcons->icon(fa::stop, options));
}

// THE VIEWPORT CONTAINER'S PRESS GOES TO THE VIEW.
bool EditorPage::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            // (`dragging = true` stood here: a write-only member nothing has
            // ever read, uninitialised until this event — deleted with the
            // play-mode flag it sat beside, SMOKE-FIX-1.)
            if (obj == sceneContainer) {
                QCoreApplication::sendEvent(sceneView->asWidget(), event);
            }

            break;
        }

        default:
            break;
    }

    return false;
}

// THE EDITOR'S EDIT TARGET (EDITOR_MULTISELECT_SPEC §2.6): the four chords act
// on the selection SET, the clipboard is the one system clipboard. The hub asks
// for it each time a chord fires on the editor space; on any other space the
// space's own target (or none) answers instead — never this one.
EditTarget EditorPage::editTarget()
{
    EditTarget t;
    t.deleteSelection = [this]() { deleteSelection(); };
    t.duplicateSelection = [this]() { duplicateSelection(); };
    t.copySelection = [this]() { copySelection(); };
    t.cutSelection = [this]() { cutSelection(); };
    t.paste = [this]() { paste(); };
    t.selectAll = [this]() {
        if (mServices && mServices->sceneEdit) mServices->sceneEdit->selectAll();
    };
    return t;
}

void EditorPage::deleteSelection()
{
    if (!mServices || !mServices->selection) return;
    const auto set = mServices->selection->selectedSet();
    if (set.size() > 1) { mServices->sceneEdit->deleteNodes(set); return; }
    mServices->sceneEdit->deleteNode(mServices->selection->selected());
}

// THE SELECTION, not the primary (EDITOR_MULTISELECT_SPEC §2.5). Both of these
// are what the toolbar buttons, the outliner's context menu and the Del/Ctrl+D
// shortcuts call, so all three act on the whole set and land as one undo step.
void EditorPage::duplicateSelection()
{
    if (!mServices || !mServices->selection) return;
    const auto set = mServices->selection->selectedSet();
    if (set.size() > 1) { mServices->sceneEdit->duplicateNodes(set); return; }
    mServices->sceneEdit->duplicateNode(mServices->selection->selected());
}

void EditorPage::copySelection()
{
    // ONE clipboard now (CLIPBOARD_SPEC D3 b): the editor writes the same
    // system clipboard the Materials graph does, as a self-identifying text
    // payload, so a copy crosses to a second instance and back. The Materials
    // space keeps its own payload shape for one release (§2.2 `graph` items are
    // P2) — its own edit target, not a second clipboard.
    if (!mServices || !mServices->clipboard || !mServices->selection) return;
    const auto result = mServices->clipboard->copyNodes(mServices->selection->selectedSet());
    if (result.ok())
        mShell->showViewportToast(tr("Copy"), tr("%1 object(s) copied").arg(result.items));
}

void EditorPage::cutSelection()
{
    if (!mServices || !mServices->clipboard || !mServices->selection) return;
    const auto result = mServices->clipboard->cutNodes(mServices->selection->selectedSet());
    if (result.ok())
        mShell->showViewportToast(tr("Cut"), tr("%1 object(s) cut").arg(result.copy.items));
}

void EditorPage::paste()
{
    if (!mServices || !mServices->clipboard) return;

    const auto result = mServices->clipboard->paste();
    // WHAT THE PASTE COULD NOT DO IS SAID OUT LOUD. A clipboard that holds
    // nothing of ours, or objects whose textures this library has never seen,
    // used to be a silent no-op — the worst possible answer for a chord.
    if (!result.error.isEmpty()) {
        mShell->showViewportToast(tr("Paste"), result.error);
        return;
    }
    if (!result.missing.isEmpty()) {
        mShell->showViewportToast(tr("Paste"),
                          tr("%1 object(s) pasted — %2 asset(s) missing from this library")
                              .arg(result.pasted.size()).arg(result.missing.size()));
        return;
    }
    if (result.pasted.isEmpty()) {
        const QString reason = result.skipped.isEmpty()
                                   ? tr("the clipboard holds nothing to paste here")
                                   : result.skipped.first().reason;
        mShell->showViewportToast(tr("Paste"), reason);
        return;
    }
    QString message = tr("%1 object(s) pasted").arg(result.pasted.size());
    if (!result.imported.isEmpty())
        message += tr(", %1 asset(s) imported").arg(result.imported.size());
    mShell->showViewportToast(tr("Paste"), message);
}
