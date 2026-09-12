/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QLayout>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>

#include "irisgl/document/scenegraph/scenenode.h"


#include "services/services.h"
#include "services/playbackservice.h"

#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/transformeditor.h"

#include "data/database/database.h"
#include "ui/panels/propertywidgets/emitterpropertywidget.h"
#include "ui/panels/propertywidgets/fogpropertywidget.h"
#include "ui/panels/propertywidgets/lightpropertywidget.h"
#include "ui/panels/propertywidgets/decalpropertywidget.h"
#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "ui/panels/propertywidgets/meshpropertywidget.h"
#include "ui/panels/propertywidgets/mobilitypropertywidget.h"
#include "ui/panels/propertywidgets/shaderpropertywidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"
#include "ui/panels/propertywidgets/physicspropertywidget.h"
#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "ui/panels/propertywidgets/worldgipropertywidget.h"
#include "ui/panels/propertywidgets/worldpostfxpropertywidget.h"
#include "ui/panels/propertywidgets/camerapostfxpropertywidget.h"
#include "ui/panels/propertywidgets/worldaapropertywidget.h"
#include "ui/panels/propertywidgets/worldmodespropertywidget.h"
#include "ui/panels/propertywidgets/worldshadowpropertywidget.h"

SceneNodePropertiesWidget::SceneNodePropertiesWidget(QWidget *parent) : QWidget(parent)
{
    widgetPropertyLayout = new QVBoxLayout(this);
    widgetPropertyLayout->setContentsMargins(0, 0, 0, 0);

    // THE PANEL TRACKS ITS DOCK, IT NEVER PUSHES BACK (owner report
    // 2026-09-08). The dock hosts this widget in a QScrollArea with
    // widgetResizable(true) and NO horizontal scrollbar, so its width is the
    // viewport's — unless its minimumSizeHint is bigger, in which case the
    // scroll area lays it out WIDER than the viewport and everything past the
    // right edge is silently unreachable. Nothing here may carry a minimum
    // width of its own; the blades' rows are fitted on the way in
    // (ui/controls/rowfit.h) and warnIfWiderThanDock() makes a regression
    // audible instead of invisible.
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    fogPropView = new FogPropertyWidget();
    fogPropView->setPanelTitle("Fog");

    worldPropView = new WorldPropertyWidget();
    worldPropView->setPanelTitle("World");
    worldPropView->expand();

	skyPropView = new SkyPropertyWidget();
	skyPropView->setPanelTitle("Sky");
	skyPropView->setDatabase(db);
	skyPropView->expand();

	// World Modes (POST_CHAIN_SPEC §9.6) sits FIRST among the quality sections:
	// it is the tier every one of them resolves through.
	worldModesPropView = new WorldModesPropertyWidget();
	worldModesPropView->setPanelTitle("World Mode");
	worldModesPropView->expand();
	// A tier writes THROUGH to the very fields the sibling World sections
	// display, and those sections read their fields ONLY when they are built.
	// Rebuild every one of them whenever a World Mode edit lands, or they keep
	// showing the pre-switch values until the node is reselected.
	//
	// DEFECT, owner-reported 2026-09-06 and fixed here: this list used to stop
	// at Anti-Aliasing / Shadows / Global Illumination — and the SKY section
	// displays two World Mode rows of its own, "Sky Detail"
	// (scene->skyBakeResolution) and "Ambient From Sky"
	// (scene->ambientFromSky). Both are written through by setMode(), neither
	// was refreshed, so switching Epic -> High changed the document and left
	// the Sky rows showing the OLD tier. The panel was the only way to see what
	// a mode did, and for those two rows it was lying. Anything that grows a
	// world-mode-backed row belongs in this list.
	connect(worldModesPropView, &WorldModesPropertyWidget::worldSettingsChanged,
	        this, [this]() {
		auto sc = scene;
		if (!sc && !!sceneNode) sc = sceneNode->getScene();
		if (!sc) return;
		if (worldAaPropView)     worldAaPropView->setScene(sc);
		if (worldShadowPropView) worldShadowPropView->setScene(sc);
		if (worldGiPropView)     worldGiPropView->setScene(sc);
		if (worldPostFxPropView) worldPostFxPropView->setScene(sc);
		if (skyPropView)         skyPropView->setScene(sc);
	});

	worldGiPropView = new WorldGiPropertyWidget();
	// THE OTHER DIRECTION of the same rule (debt L6 item 4, found by the Rayon
	// lane): the Rayon section writes World Mode registry rows — the technique,
	// the quality, the irradiance field, the tier — and the World Mode section
	// lists every one of them with its pin mark. Without this, a Rayon edit left
	// those rows showing the pre-edit values until the world was reselected.
	connect(worldGiPropView, &WorldGiPropertyWidget::worldSettingsChanged, this, [this]() {
		auto sc = scene;
		if (!sc && !!sceneNode) sc = sceneNode->getScene();
		if (!sc) return;
		if (worldModesPropView) worldModesPropView->setScene(sc);
		if (worldPostFxPropView) worldPostFxPropView->setScene(sc);
	});
	// RAYON is the product name for realtime global illumination
	// (GI_UNIFIED_SPEC.md; the naming rule is that it is ALWAYS subtitled, so
	// nobody reads it as hardware ray tracing). The section is one switch, one
	// quality dial and the update budget, with everything they consume behind
	// its own Advanced disclosure.
	worldGiPropView->setPanelTitle("Rayon — Realtime Global Illumination");
	worldGiPropView->expand();

	// POST PROCESS (fix wave 2026-09-07 item 8): every post effect and its
	// parameters in one section, generated from the same registry the World
	// Mode section is. It sits between GI and Anti-Aliasing because that is
	// where it sits in the frame — after the lighting solve, before the AA.
	worldPostFxPropView = new WorldPostFxPropertyWidget();
	worldPostFxPropView->setPanelTitle("Post Process");
	worldPostFxPropView->expand();
	// The two sections show the SAME on/off rows (this one groups the chain,
	// the World Mode one lists the whole tier), so each has to re-read after
	// the other writes.
	connect(worldPostFxPropView, &WorldPostFxPropertyWidget::worldSettingsChanged,
	        this, [this]() {
		auto sc = scene;
		if (!sc && !!sceneNode) sc = sceneNode->getScene();
		if (!sc) return;
		if (worldModesPropView) worldModesPropView->setScene(sc);
	});

	worldAaPropView = new WorldAaPropertyWidget();
	worldAaPropView->setPanelTitle("Anti-Aliasing");
	worldAaPropView->expand();

	worldShadowPropView = new WorldShadowPropertyWidget();
	worldShadowPropView->setPanelTitle("Shadows");
	worldShadowPropView->expand();

    transformPropView = new AccordianBladeWidget();
    transformPropView->setPanelTitle("Transformation");
    transformWidget = transformPropView->addTransformControls();
    transformPropView->expand();

    // MOVEMENT (REALTIME_REFLECTIONS_SPEC §3.3), right under Transformation:
    // "does this move?" is a property of the OBJECT, not of its mesh, so it is
    // its own blade and every node kind gets it.
    mobilityPropView = new MobilityPropertyWidget();
    mobilityPropView->setPanelTitle("Movement");
    mobilityPropView->expand();

    physicsPropView = new PhysicsPropertyWidget();
    physicsPropView->setPanelTitle("Physics Properties");

    meshPropView = new MeshPropertyWidget();
    meshPropView->setPanelTitle("Mesh Properties");
    meshPropView->expand();

    lightPropView = new LightPropertyWidget();
    lightPropView->setPanelTitle("Light");
    lightPropView->setDatabase(db);
    lightPropView->expand();

    decalPropView = new DecalPropertyWidget();
    decalPropView->setPanelTitle("Decal");
    decalPropView->expand();

    emitterPropView = new EmitterPropertyWidget();
    emitterPropView->setPanelTitle("Emitter");
    emitterPropView->setDatabase(db);
    emitterPropView->expand();

    // CAMERA_LENS_SPEC §4/§5: a selected scene camera grades its own shot.
    // This is the camera's first properties section — cameras had none before
    // (the transform editor was all a selected camera showed).
    cameraPostFxPropView = new CameraPostFxPropertyWidget();
    cameraPostFxPropView->setPanelTitle("Exposure & Post");
    cameraPostFxPropView->expand();

    shaderPropView = new ShaderPropertyWidget();
    shaderPropView->setPanelTitle("Shader Definitions");
    shaderPropView->setDatabase(db);
    shaderPropView->expand();

    // SELECTION COST (perf regression, owner session 2026-09-08: 1 fps and
    // 2-7 s UI stalls after an hour). Every blade above is a PERMANENT child of
    // this panel from here on, and selection only ever changes which of them
    // the layout holds — see clearLayout() for why that matters. Adopting them
    // here is the one and only reparent each of them will ever see, and it
    // happens before any of them has painted, i.e. before the style has
    // attached a single focus frame.
    for (QWidget *blade : bladeWidgets()) {
        adoptBlade(blade);
    }

    setLayout(widgetPropertyLayout);
}

/// Every blade this panel owns for the lifetime of the window. The material
/// blade is NOT here: it is built on demand (the first mesh selection) and
/// adopted then.
QVector<QWidget *> SceneNodePropertiesWidget::bladeWidgets() const
{
    return {
        fogPropView, worldPropView, skyPropView,
        worldModesPropView, worldGiPropView, worldPostFxPropView,
        worldAaPropView, worldShadowPropView, transformPropView,
        mobilityPropView,
        physicsPropView, meshPropView, lightPropView, decalPropView,
        emitterPropView, cameraPostFxPropView, shaderPropView
    };
}

/// Makes a blade a permanent, hidden child. Hidden EXPLICITLY, so that adding
/// it to the layout later does not show it by accident and — more importantly —
/// so that Qt's layout machinery leaves its visibility to mount()/clearLayout().
void SceneNodePropertiesWidget::adoptBlade(QWidget *blade)
{
    if (!blade) return;
    if (blade->parentWidget() != this) blade->setParent(this);
    blade->hide();
}

/// Puts an already-adopted blade on screen. The ONLY two things a selection
/// change does to a blade are this and its inverse in clearLayout(); neither
/// touches the parent, so neither sends a single QEvent::ParentChange.
void SceneNodePropertiesWidget::mount(QWidget *blade)
{
    if (!blade) return;
    Q_ASSERT(blade->parentWidget() == this);
    widgetPropertyLayout->addWidget(blade);
    blade->show();
}

void SceneNodePropertiesWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        skyPropView->setScene(this->scene);
    }
}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode;

        widgetPropertyLayout->setContentsMargins(0, 0, 0, 0);
        clearLayout(this->layout());

        if (sceneNode->isRootNode()) {
            fogPropView->setScene(sceneNode->getScene());
            worldPropView->setScene(sceneNode->getScene());
            worldModesPropView->setSceneView(sceneView);
            worldModesPropView->setScene(sceneNode->getScene());
            worldGiPropView->setSceneView(sceneView);
            worldGiPropView->setScene(sceneNode->getScene());
            worldPostFxPropView->setSceneView(sceneView);
            worldPostFxPropView->setScene(sceneNode->getScene());
            worldAaPropView->setSceneView(sceneView);
            worldAaPropView->setScene(sceneNode->getScene());
            worldShadowPropView->setSceneView(sceneView);
            worldShadowPropView->setScene(sceneNode->getScene());
            mount(worldPropView);
            // The world's sky: re-bind, because the same panel may have been
            // showing a LIBRARY sky asset since the last time the world was
            // selected (one implementation, two bindings).
            skyPropView->setScene(sceneNode->getScene());
            mount(skyPropView);
            mount(worldModesPropView);
            mount(worldGiPropView);
            mount(worldPostFxPropView);
            mount(worldAaPropView);
            mount(worldShadowPropView);
            mount(fogPropView);
        }
        else {
            transformWidget->setSceneNode(sceneNode);
            mount(transformPropView);
            // EVERY node kind: the resolution has an answer for all of them,
            // and a light or an emitter is exactly the kind of thing an author
            // needs to pin by hand.
            mobilityPropView->setSceneNode(sceneNode);
            mount(mobilityPropView);

            switch (sceneNode->getSceneNodeType()) {
                case iris::SceneNodeType::Light: {
                    lightPropView->setSceneNode(sceneNode);
                    mount(lightPropView);
                    break;
                }

                case iris::SceneNodeType::Decal: {
                    decalPropView->setDatabase(db);
                    decalPropView->setProject(project);
                    decalPropView->setServices(services);
                    decalPropView->setSceneNode(sceneNode);
                    mount(decalPropView);
                    break;
                }

                case iris::SceneNodeType::Empty: {
                    physicsPropView->setSceneNode(sceneNode);
                    physicsPropView->setSceneView(sceneView);
                    mount(physicsPropView);
                    break;
                }

                case iris::SceneNodeType::Mesh: {
                    // THE LEAK behind the session-long slowdown: this panel was
                    // built fresh for EVERY mesh selection and the old one was
                    // handed to clearLayout(), which orphaned it with
                    // setParent(nullptr) — a live, parentless widget tree that
                    // nothing ever deleted. An hour of clicking around left
                    // hundreds of them (and their focus frames, their event
                    // filters and their property listeners) alive in the
                    // process. Build it ONCE and refill it; the rebuild-per-node
                    // that made it look disposable is what clearPanel() does,
                    // and materialChanged() has always used exactly that path on
                    // the live panel.
                    if (!materialPropView) {
                        materialPropView = new MaterialPropertyWidget();
                        materialPropView->setPanelTitle("Material");
                        materialPropView->expand();
                        adoptBlade(materialPropView);
                    }
                    materialPropView->setDatabase(db);
                    materialPropView->setProject(project);
                    materialPropView->setServices(services);
                    // Drop the previous node's rows (deleteLater, so nothing is
                    // freed under a signal that is still on the stack) before
                    // the new ones are appended.
                    materialPropView->clearPanel(materialPropView->layout());

                    physicsPropView->setSceneNode(sceneNode);
                    physicsPropView->setSceneView(sceneView);
                    meshPropView->setSceneView(sceneView);
                    meshPropView->setSceneNode(sceneNode);
                    materialPropView->setSceneNode(sceneNode);

                    if (!(services && services->playback && services->playback->isSimulationRunning())) {
                        mount(physicsPropView);
                    }

                    mount(meshPropView);
                    mount(materialPropView);
                    break;
                }

                case iris::SceneNodeType::Camera: {
                    cameraPostFxPropView->setSceneView(sceneView);
                    cameraPostFxPropView->setSceneNode(sceneNode);
                    mount(cameraPostFxPropView);
                    break;
                }

                case iris::SceneNodeType::ParticleSystem: {
                    emitterPropView->setSceneNode(sceneNode);
                    mount(emitterPropView);
                    break;
                }

                default: break;
            }
        }

        widgetPropertyLayout->addStretch();
        warnIfWiderThanDock();
    }
    else {
        clearLayout(this->layout());
    }
}

/// THE FAILURE IS NEVER SILENT AGAIN. A panel wider than its dock is invisible
/// by construction: the rows are laid out past the right edge of a scroll area
/// that has no horizontal bar, so they are on screen, not hidden, and not
/// reachable (root cause of "the World blades show no controls", 2026-09-08 —
/// one label row asked for 3674 px of a 315 px dock and took every other row
/// with it). Nothing about that shows up in a screenshot, a visibility dump or
/// a widget count, so it says so in the log, naming the blade that did it.
void SceneNodePropertiesWidget::warnIfWiderThanDock()
{
    QWidget *viewport = nullptr;
    for (QWidget *p = parentWidget(); p; p = p->parentWidget()) {
        if (auto *area = qobject_cast<QScrollArea *>(p)) { viewport = area->viewport(); break; }
    }
    const int budget = viewport ? viewport->width() : width();
    if (budget <= 0) return;

    const int need = minimumSizeHint().width();
    if (need <= budget) { lastWidthWarning.clear(); return; }

    QString worst;
    int worstWidth = 0;
    for (QWidget *blade : bladeWidgets()) {
        if (!blade || !blade->isVisibleTo(this)) continue;
        const int w = blade->minimumSizeHint().width();
        if (w > worstWidth) { worstWidth = w; worst = blade->metaObject()->className(); }
    }
    const QString key = QStringLiteral("%1/%2/%3").arg(worst).arg(need).arg(budget);
    if (key == lastWidthWarning) return;   // once per offender, not once per resize
    lastWidthWarning = key;
    qWarning("Properties panel does not fit its dock: needs %d px, has %d px "
             "(widest section: %s, %d px). Rows past the right edge are unreachable.",
             need, budget, qPrintable(worst), worstWidth);
}

void SceneNodePropertiesWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    warnIfWiderThanDock();
}

void SceneNodePropertiesWidget::setAssetItem(QListWidgetItem *item)
{
    if (!item) return;

    if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Shader)) {
        clearLayout(this->layout());
        shaderPropView->setShaderGuid(item->data(MODEL_GUID_ROLE).toString());
        mount(shaderPropView);
        widgetPropertyLayout->addStretch();
    }
    else if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Sky))
    {
        clearLayout(this->layout());
		skyPropView->setSkyAlongWithProperties(item->data(MODEL_GUID_ROLE).toString(),
											   static_cast<iris::SkyType>(item->data(SKY_TYPE_ROLE).toInt()));
		mount(skyPropView);
		widgetPropertyLayout->addStretch();
    }
}

void SceneNodePropertiesWidget::refreshMaterial(const QString &matName)
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh
        && materialPropView) {
        materialPropView->forceShaderRefresh(matName);
    }
}

void SceneNodePropertiesWidget::refreshFromDocument()
{
    // Deferred: an undo can arrive from inside a control's own signal (a
    // shortcut handled while a combo popup is closing), and rebuilding a blade
    // there is the crash the sky panel's queued rebuild exists to avoid.
    QPointer<SceneNodePropertiesWidget> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) return;
        if (!!self->sceneNode) self->setSceneNode(self->sceneNode);
        else if (!!self->scene) self->setScene(self->scene);
    });
}

void SceneNodePropertiesWidget::refreshTransform()
{
	if (transformWidget) {
		transformWidget->refreshUi();
	}
}

void SceneNodePropertiesWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
    if (skyPropView) skyPropView->wireViewportEvents(sceneView);
}

void SceneNodePropertiesWidget::setServices(StudioServices *services)
{
    this->services = services;
    if (transformWidget) transformWidget->setServices(services);
    // World Mode edits are undoable (WorldModeCommand) — the section needs the
    // undo stack, and like every other panel here it is built in the CONSTRUCTOR,
    // which runs before this setter.
    if (worldModesPropView) worldModesPropView->setServices(services);
    // EVERY properties row is undoable (debt L6 / N5), so every panel that owns
    // rows needs the stack — not just the two that had it.
    if (worldGiPropView) worldGiPropView->setServices(services);
    if (worldPropView) worldPropView->setServices(services);
    if (fogPropView) fogPropView->setServices(services);
    if (worldAaPropView) worldAaPropView->setServices(services);
    if (worldShadowPropView) worldShadowPropView->setServices(services);
    if (worldPostFxPropView) worldPostFxPropView->setServices(services);
    if (lightPropView) lightPropView->setServices(services);
    if (meshPropView) meshPropView->setServices(services);
    if (mobilityPropView) mobilityPropView->setServices(services);
    if (physicsPropView) physicsPropView->setServices(services);
    if (emitterPropView) emitterPropView->setServices(services);
    if (cameraPostFxPropView) cameraPostFxPropView->setServices(services);
    if (skyPropView) skyPropView->eventBus = services ? services->eventBus : nullptr;
    // Sun coupling (re-audit F5): both sky panels carry the "drive a
    // directional light" row, which needs the selection and the undo stack.
    if (skyPropView) skyPropView->setServices(services);
}

void SceneNodePropertiesWidget::setDatabase(Database *db)
{
    this->db = db;
    // FORWARD, DO NOT JUST STORE. Every panel here is built in the CONSTRUCTOR,
    // which runs before this setter — so the ctor's `setDatabase(db)` calls
    // hand them the member while it is still NULL, and a panel that only got
    // it there never has a library at all. That was silent damage, not a
    // theoretical one (code review F-P1): the sky section's equirect pick and
    // its cubemap slots returned early on `!db`, the Sky Presets apply did
    // nothing, a sky ASSET could not be edited, and the emitter's image row and
    // the shader panel dereferenced the null outright.
    //
    // The rule for this panel from here on: a child that needs the library is
    // re-pushed HERE, and the ctor's call stays only as the "born with
    // whatever we have" case.
    if (lightPropView) lightPropView->setDatabase(db);
    if (skyPropView) skyPropView->setDatabase(db);
    if (emitterPropView) emitterPropView->setDatabase(db);
    if (shaderPropView) shaderPropView->setDatabase(db);
    if (decalPropView) decalPropView->setDatabase(db);
    if (materialPropView) materialPropView->setDatabase(db);
}

void SceneNodePropertiesWidget::setProject(Project *project)
{
    // Phase 4: every panel that used to read the Globals::project static now
    // carries the pointer (AccordianBladeWidget::project, which its add*()
    // helpers forward to the controls they build).
    this->project = project;
    if (worldPropView)    worldPropView->setProject(project);
    if (skyPropView)      skyPropView->setProject(project);
    if (emitterPropView)  emitterPropView->setProject(project);
    if (shaderPropView)   shaderPropView->setProject(project);
    if (lightPropView)    lightPropView->setProject(project);
    // materialPropView is created on demand in setSceneNode() and gets the
    // pointer there (the member is not null-initialised).
}

void SceneNodePropertiesWidget::acceptCubemapTexturesFromSkyPresets(QStringList guids)
{
	QStringList fileNames;

	for (auto guid : guids) {
		fileNames.append(db->fetchAsset(guid).name);
	}

	db->removeDependenciesByType(scene->skyGuid, ModelTypes::Texture);

	skyPropView->skyTypeChanged(static_cast<int>(iris::SkyType::CUBEMAP));
	for (int i = 0; i < 6; i++) {
		skyPropView->onSlotChanged(fileNames[i], guids[i], i);
	}
}

/**
 * Takes every blade back off the layout, WITHOUT reparenting any of them.
 *
 * THE PERF REGRESSION (owner session 2026-09-08 — 1 fps, 2-7 s UI stalls after
 * an hour of use; watchdog backtraces all in
 * WidgetWithFocusFrameEventFilter::refreshFocusFrame under this function):
 * this used to call `widget->setParent(nullptr)` on each blade, i.e. it
 * DETACHED a live widget subtree on every single selection change and
 * re-attached it a few lines later in setSceneNode(). Each of those two
 * reparents sends a QEvent::ParentChange to the blade, and Qlementine installs
 * one WidgetWithFocusFrameEventFilter per focusable descendant, every one of
 * which watches its ancestors — so ONE selection change fired the ancestor
 * handler dozens of times, twice, and each firing re-derived and re-parented a
 * QFocusFrame and re-installed its event filters along the whole chain. It also
 * left the frames stranded in a hierarchy their widget had left, which is the
 * "QWidget::mapTo(): parent must be in parent hierarchy" flood (164,651
 * warnings in 13 minutes) the qlementine fork was pulled in to fix.
 *
 * Hiding instead of orphaning is the honest shape: the blades are this panel's
 * permanent children (adopted in the constructor / at first use), a selection
 * change only decides which of them the layout holds, and the widget hierarchy
 * never changes at all. Zero ParentChange events, zero focus-frame work, no
 * growth.
 *
 * @param layout
 */
void SceneNodePropertiesWidget::clearLayout(QLayout *layout)
{
    if (layout == nullptr) return;

    while (auto item = layout->takeAt(0)) {
        if (auto widget = item->widget()) {
            // NOT setParent(nullptr) — see above. Explicitly hidden, so the
            // next mount() has to show it deliberately.
            widget->hide();
        }

        if (auto childLayout = item->layout()) this->clearLayout(childLayout);
        delete item;
    }

    //delete layout;
}
