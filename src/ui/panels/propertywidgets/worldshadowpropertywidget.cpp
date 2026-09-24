/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldshadowpropertywidget.h"
#include "ui/panels/propertyrows.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/shadowmap.h"

#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/labelwidget.h"
#include "viewport/ieditorviewport.h"
#include "services/worldmodes.h"
#include "ui/panels/propertywidgets/panelundo.h"
#include "ui/panels/propertywidgets/rowundo.h"

#include <QSignalBlocker>

namespace {
// Combo rows in display order -> scene->shadowResolution values. 0 = Auto.
// The engine accepts up to 8192, but 8192 is a ~940 MB allocation: scripts may
// ask for it (world.setShadowResolution warns), the UI does not offer it.
const int kShadowRows[] = { 0, 1024, 2048, 4096 };
const int kShadowRowCount = int(sizeof(kShadowRows) / sizeof(kShadowRows[0]));
int shadowRowFor(int resolution)
{
    for (int i = 0; i < kShadowRowCount; ++i)
        if (kShadowRows[i] == resolution) return i;
    return 0;   // anything a script set outside the offered set reads as Auto
}
}

WorldShadowPropertyWidget::WorldShadowPropertyWidget()
    : contactRows([this]() { return scene; }, [this]() { return services; },
                  [this]() { contactEdited(); }, [this]() { return !loading; })
{
}

// After a Sun Contact gesture, and after every undo/redo of one. Two frames
// first, so the renderer's answer (running, its resolution) is the edit's and
// not the frame before it — the Shadow Quality combo's refresh, for the same
// reason. (A command's FIRST redo is the edit itself and runs no refresh, so
// the rows' own signals call this too; see build().)
void WorldShadowPropertyWidget::contactEdited()
{
    if (loading || !scene) return;
    if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
    refreshRows();
}

int WorldShadowPropertyWidget::atlasMegabytes(int resolution)
{
    if (resolution <= 0) return 0;
    // OgreEngine::createShadowNode builds R x 3.5R at PFG_D32_FLOAT: split 0 and
    // the two focused maps at RxR, splits 1-3 at R/2. 4 bytes per texel.
    return int(qRound(4.0 * 3.5 * double(resolution) * double(resolution) / (1024.0 * 1024.0)));
}

void WorldShadowPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        build();
        refreshRows();
    } else {
        this->scene.clear();
    }
}

void WorldShadowPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

int WorldShadowPropertyWidget::derivedFromLights() const
{
    if (!scene) return 0;
    // SceneMirror's policy, verbatim: area lights cannot cast, "None" does not
    // ask, and the largest remaining request wins.
    int best = 0;
    for (const auto &l : scene->lights) {
        // A Sky Light cannot cast either — it has no place to cast FROM.
        if (l.isNull() || l->lightType == iris::LightType::Area ||
            l->lightType == iris::LightType::Sky) continue;
        if (!l->shadowMap || l->shadowMap->shadowType == iris::ShadowMapType::None) continue;
        best = std::max(best, l->shadowMap->resolution);
    }
    return best;
}

void WorldShadowPropertyWidget::build()
{
    if (qualitySelector) return;   // built once, refilled from here on

    qualitySelector = this->addComboBox("Shadow Quality");
    PropertyRows::identify(qualitySelector, QStringLiteral("world.shadowResolution"),
                           { QStringLiteral("shadows"), QStringLiteral("resolution"),
                             QStringLiteral("atlas") });
    qualitySelector->addItem("Auto (from lights)");
    qualitySelector->addItem("1024");
    qualitySelector->addItem("2048");
    qualitySelector->addItem("4096");
    qualitySelector->setToolTip(
        QStringLiteral("One shadow atlas serves every light in the scene. Auto sizes it from the "
                       "largest per-light Shadow Size; an explicit choice overrides that."));
    connect(qualitySelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldShadowPropertyWidget::onQualityChanged);

    sunRow = this->addLabel("Sun", QString());
    secondaryRow = this->addLabel("Other Directionals", QString());
    autoRow = this->addLabel("Auto Resolves To", QString());
    memoryRow = this->addLabel("Atlas Memory", QString());
    mapsRow = this->addLabel("Shadow Maps", QString());
    for (LabelWidget *row : { sunRow, secondaryRow, autoRow, memoryRow, mapsRow })
        if (row) PropertyRows::setPanelVisible(row, false);

    // ---- SUN CONTACT (PHOTON-RAYS-1's row; world.sunContact) ----------------
    // ORDER MATTERS below: rowundo::bind names every row by its binding key
    // ("sunContact" for all three), and the PropertyRows::identify AFTER it is
    // what gives each its own key — swap them and the three rows share one key,
    // which the filter tolerates and editor.propertyRow refuses by name.
    // Three rows over one document block, each gesture one undo step through
    // the "sunContact" key the verb writes. The range control carries the
    // verb's band, so it cannot offer a value the verb would refuse.
    const QStringList contactWords = { QStringLiteral("contact"), QStringLiteral("sun"),
                                       QStringLiteral("rays") };
    contactEnabled = this->addCheckBox("Sun Contact", false);
    contactEnabled->setToolTip(QStringLiteral(
        "Hard contact shadows traced from the sun: one hardware ray per pixel closes the gap of "
        "light a shadow map leaves where an object meets the ground. Off by default; it needs "
        "ray tracing (World > Ray Tracing, and a GPU that has it) and is never drawn in VR."));
    rowundo::bind(contactEnabled, contactRows(QStringLiteral("sunContact"), tr("Sun Contact"),
        [this](const QVariant &v) {
            return withContact([&v](iris::SunContact &c) { c.enabled = v.toBool(); });
        }));
    PropertyRows::identify(contactEnabled, QStringLiteral("sunContact.enabled"), contactWords);

    contactRange = this->addFloatValueSlider("Contact Range (m)", iris::kSunContactMinRange,
                                             iris::kSunContactMaxRange, 2.0f);
    contactRange->setDecimals(2);
    contactRange->setToolTip(QStringLiteral(
        "How far, in metres, a contact ray looks for something between the surface and the sun. "
        "Beyond it the shadow map answers alone."));
    rowundo::bind(contactRange, contactRows(QStringLiteral("sunContact"), tr("Sun Contact Range"),
        [this](const QVariant &v) {
            return withContact([&v](iris::SunContact &c) { c.range = v.toFloat(); });
        }));
    PropertyRows::identify(contactRange, QStringLiteral("sunContact.range"), contactWords);

    // Row index = iris::SunContactResolution (Auto 0, Full 1, Half 2).
    contactResolution = this->addComboBox("Contact Resolution");
    contactResolution->addItem("Auto (follows the tier)");
    contactResolution->addItem("Full (one ray per pixel)");
    contactResolution->addItem("Half (one ray per 2x2 block)");
    contactResolution->setToolTip(QStringLiteral(
        "Auto traces at half resolution at the Low and Medium GI quality and at full resolution "
        "at High."));
    rowundo::bind(contactResolution, contactRows(QStringLiteral("sunContact"),
                                                 tr("Sun Contact Resolution"),
        [this](const QVariant &v) {
            return withContact([&v](iris::SunContact &c) {
                c.resolution = iris::SunContactResolution(qBound(0, v.toInt(), 2));
            });
        }));
    PropertyRows::identify(contactResolution, QStringLiteral("sunContact.resolution"),
                           contactWords);

    // THE EDIT'S OWN REFRESH: connected AFTER rowundo::bind, so the document
    // already holds the gesture's value when these run.
    connect(contactEnabled, &CheckBoxWidget::valueChanged, this, [this](bool) { contactEdited(); });
    connect(contactRange, &HFloatSliderWidget::valueChangeEnd, this,
            [this](float) { contactEdited(); });
    connect(contactResolution, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this,
            [this](int) { contactEdited(); });

    contactStatus = this->addLabel("Contact Status", QString());
    PropertyRows::identify(contactStatus, QStringLiteral("sunContact.status"), contactWords);
    PropertyRows::setPanelVisible(contactStatus, false);
}

QVariant WorldShadowPropertyWidget::withContact(
    const std::function<void(iris::SunContact &)> &edit) const
{
    if (!scene) return QVariant();
    iris::SunContact c = scene->sunContact;
    edit(c);
    return iris::SunContact::clamped(c).toJson().toVariantMap();
}

// THE SUN CONTACT ROWS, re-read in place: the document block (what
// world.sunContact() returns), then what the renderer did with it (its `live`
// half, from the same two engine calls).
void WorldShadowPropertyWidget::refreshSunContact()
{
    if (!scene || !contactEnabled) return;
    const iris::SunContact &c = scene->sunContact;
    contactEnabled->setValue(c.enabled);
    contactRange->setValue(c.range);
    {
        const QSignalBlocker quiet(contactResolution);
        contactResolution->setCurrentIndex(int(c.resolution));
    }

    // No renderer (headless, before the first frame): the rows are the
    // document's, live, and there is nothing to report.
    IEditorViewport::SunContactInfo st;
    if (sceneView && sceneView->isInitialized()) st = sceneView->sunContactInfo();
    const bool rays = st.available ? st.rays : true;
    // THE ROWS THAT CANNOT ACT ARE GREYED. Without rays the range and the
    // resolution mean nothing; the switch stays live while it is ON so a scene
    // authored elsewhere can always be turned off here.
    contactRange->setEnabled(rays);
    contactResolution->setEnabled(rays);
    contactEnabled->setEnabled(rays || c.enabled);

    QString status;
    if (st.available && !rays) {
        status = QStringLiteral("Needs rays: this scene is not ray traced here (World > Ray "
                                "Tracing is Off, or this machine has no ray-tracing GPU). The "
                                "shadow map answers alone.");
    } else if (st.available && c.enabled && st.on && !st.running) {
        status = QStringLiteral("Not running: %1.").arg(st.reason);
    } else if (st.available && c.enabled && st.running) {
        status = QStringLiteral("Tracing %1 x %2 (%3), out to %4 m")
                     .arg(st.width).arg(st.height)
                     .arg(st.divisor > 1 ? QStringLiteral("one ray per 2x2 block")
                                         : QStringLiteral("one ray per pixel"))
                     .arg(double(st.range));
    }
    contactStatus->setText(status);
    PropertyRows::setPanelVisible(contactStatus, !status.isEmpty());
}

// The READ-BACK rows, re-read in place. Nothing here is created or destroyed:
// a row with nothing to say hides, which is what lets an edit refresh the
// blade without deleting the combo whose signal is still on the stack.
void WorldShadowPropertyWidget::refreshRows()
{
    if (!scene || !qualitySelector) return;
    loading = true;
    {
        const QSignalBlocker quiet(qualitySelector);
        qualitySelector->setCurrentIndex(shadowRowFor(scene->shadowResolution));
    }

    // ---- THE SUN, in words (owner Q1d) --------------------------------
    // Document facts, so they are right with no renderer: one resolver answers
    // both rows (iris::Scene::sunLight / secondaryDirectionals).
    if (sunRow) {
        auto sun = scene->sunLight();
        const QString reason = scene->sunReason();
        if (!sun) {
            sunRow->setText(QStringLiteral("No sun in this scene — objects are lit by the lamps "
                                           "and the sky. That is a perfectly ordinary scene; "
                                           "adding a directional light makes it the sun."));
        } else if (reason == QLatin1String("pinned")) {
            sunRow->setText(QStringLiteral("%1 — chosen by hand").arg(sun->getName()));
        } else {
            sunRow->setText(QStringLiteral("%1 — chosen automatically (lowest Forward Shading "
                                           "Priority, %2)")
                                .arg(sun->getName())
                                .arg(sun->forwardShadingPriority));
        }
        PropertyRows::setPanelVisible(sunRow, true);
    }
    if (secondaryRow) {
        const auto others = scene->secondaryDirectionals();
        if (others.isEmpty()) {
            PropertyRows::setPanelVisible(secondaryRow, false);
        } else {
            QStringList names;
            for (const auto &l : others)
                names << QStringLiteral("%1 (%2)").arg(l->getName()).arg(l->forwardShadingPriority);
            secondaryRow->setText(
                QStringLiteral("%1 directional lights. Only the sun casts a shadow — the "
                               "renderer has one directional slot. The others: %2. Set Forward "
                               "Shading Priority on a light to choose.")
                    .arg(others.size() + 1)
                    .arg(names.join(QStringLiteral(", "))));
            PropertyRows::setPanelVisible(secondaryRow, true);
        }
    }

    const int derived = derivedFromLights();
    int effective = scene->shadowResolution > 0 ? scene->shadowResolution : derived;
    if (sceneView && sceneView->isInitialized()) {
        const int live = sceneView->shadowResolution();
        if (live > 0) effective = live;
    }
    if (autoRow) {
        // Auto has to say what it derived or the row is a mystery.
        PropertyRows::setPanelVisible(autoRow, scene->shadowResolution == 0);
        autoRow->setText(derived > 0
                             ? QStringLiteral("%1 (largest light request)").arg(effective)
                             : QStringLiteral("no shadow-casting light yet"));
    }

    // THE ATLAS, AS THE RENDERER BUILT IT (SHADOW_TOOLING_SPEC.md §4.2). The
    // layout is packed into columns now, not a fixed R x 3.5R strip, so the
    // live figures are read back rather than computed whenever an engine is
    // there to ask; atlasMegabytes() stays the offline estimate.
    IEditorViewport::ShadowStatusInfo st;
    if (sceneView && sceneView->isInitialized()) st = sceneView->shadowStatus();
    if (memoryRow) {
        if (st.available && st.atlasWidth > 0) {
            // atlasBytes counts the planar mirrors' own atlases too (they hold
            // the same number of lamp maps since ENGINE_CACHE_POLICY D3).
            QString text = QStringLiteral("~%1 MB VRAM (%2 x %3, %4 point/spot maps")
                               .arg(int(st.atlasBytes / (1024 * 1024)))
                               .arg(st.atlasWidth).arg(st.atlasHeight).arg(st.focusedMaps);
            if (st.reflectAtlasBytes > 0)
                text += QStringLiteral("; %1 MB of it for mirrors")
                            .arg(int(st.reflectAtlasBytes / (1024 * 1024)));
            memoryRow->setText(text + QStringLiteral(")"));
            PropertyRows::setPanelVisible(memoryRow, true);
        } else if (effective > 0) {
            memoryRow->setText(QStringLiteral("~%1 MB VRAM (%2 x %3)")
                                   .arg(atlasMegabytes(effective))
                                   .arg(effective).arg(qRound(effective * 3.5)));
            PropertyRows::setPanelVisible(memoryRow, true);
        } else {
            PropertyRows::setPanelVisible(memoryRow, false);
        }
    }

    // THE EXCEEDED CASE, said out loud (owner decision D5: panel + status +
    // log, no toast). Before this, a scene with more shadow-casting lamps than
    // the atlas has maps simply showed fewer shadows than it had lights, and
    // WHICH lamp went dark changed as the camera moved.
    const int unmapped = st.available ? int(st.unmapped.size()) : 0;
    if (mapsRow) {
        if (unmapped > 0) {
            mapsRow->setText(QStringLiteral("%1 of %2 shadow-casting lights have a map — %3 cast "
                                            "no shadow. Raise Shadow Map Budget in World Modes, "
                                            "or turn off Cast Shadows on distant lights.")
                                 .arg(st.casters - unmapped).arg(st.casters).arg(unmapped));
            PropertyRows::setPanelVisible(mapsRow, true);
        } else if (st.available && st.casters > 0) {
            mapsRow->setText(QStringLiteral("%1 of %1 shadow-casting lights have a map")
                                 .arg(st.casters));
            PropertyRows::setPanelVisible(mapsRow, true);
        } else {
            PropertyRows::setPanelVisible(mapsRow, false);
        }
    }
    refreshSunContact();
    loading = false;
}

void WorldShadowPropertyWidget::onQualityChanged(int row)
{
    if (loading || !scene || row < 0 || row >= kShadowRowCount) return;
    const int resolution = kShadowRows[row];
    // The registry write pins the row (POST_CHAIN_SPEC §9.1) and the command
    // carries both halves, so an undo hands the row back to the tier as well
    // as restoring the number. The engine rebuilds its shadow node on change,
    // which is why this is a combo and not a slider.
    panelundo::runWorldModeEdit(services, scene, tr("Shadow Quality"),
        [this, resolution]() {
            worldmodes::setRowValue(scene, QStringLiteral("shadowResolution"), resolution);
        },
        [this]() {
            if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
            refreshRows();
        });
}
