/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/camerapostfxpropertywidget.h"

#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"

#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/labelwidget.h"
#include "viewport/ieditorviewport.h"

namespace {

/// The camera the selection is on, and the SCENE it belongs to — the world half
/// of every "inherits" answer below.
iris::ScenePtr sceneOf(const QSharedPointer<iris::CameraNode> &camera)
{
    return camera ? camera->getScene() : iris::ScenePtr();
}

/// Is this row served by the renderer at all? A row declared but not
/// implemented (POST_CHAIN_SPEC §9.2) may still be authored — files stay
/// forward-compatible — but the panel must not pretend it does anything.
bool rowAvailable(const QString &key)
{
    const worldmodes::ParamRow *p = worldmodes::postFxParam(key);
    const QString rowId = p ? p->ownerRowId : key;
    const worldmodes::Row *r = worldmodes::row(rowId);
    return r ? r->available : true;
}

QString labelFor(const QString &key)
{
    if (const worldmodes::ParamRow *p = worldmodes::postFxParam(key)) return p->label;
    if (const worldmodes::Row *r = worldmodes::row(key)) return r->label;
    return key;
}

QString tooltipFor(const QString &key)
{
    if (const worldmodes::ParamRow *p = worldmodes::postFxParam(key)) return p->doc;
    if (const worldmodes::Row *r = worldmodes::row(key)) return r->cost;
    return QString();
}

}   // namespace

CameraPostFxPropertyWidget::CameraPostFxPropertyWidget()
{
}

void CameraPostFxPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> node)
{
    camera = node.dynamicCast<iris::CameraNode>();
    rebuild();
}

void CameraPostFxPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

QString CameraPostFxPropertyWidget::inheritedText(const QString &key) const
{
    const iris::ScenePtr scene = sceneOf(camera);
    if (!scene) return QStringLiteral("world");
    if (const worldmodes::ParamRow *p = worldmodes::postFxParam(key))
        return p->get ? QString::number(p->get(scene), 'f', p->decimals) : QStringLiteral("world");
    if (const worldmodes::Row *r = worldmodes::row(key)) {
        const int v = worldmodes::resolved(scene, *r);
        if (r->type == worldmodes::RowType::Bool) return v ? QStringLiteral("on") : QStringLiteral("off");
        for (const worldmodes::EnumOption &o : r->options)
            if (o.value == v) return o.label;
        return QString::number(v);
    }
    return QStringLiteral("world");
}

void CameraPostFxPropertyWidget::rebuild()
{
    clearPanel(this->layout());
    if (!camera) return;

    // ---- §4, the exposure block -------------------------------------------
    // Three stops-based numbers and a mode. NOT the same unit as the World
    // panel's Exposure row (which is the post chain's own natural-log value) —
    // the tooltip says so, because the two rows sit two panels apart and read
    // like the same dial.
    {
        auto *mode = this->addComboBox(QStringLiteral("Exposure"));
        mode->addItem(QStringLiteral("Inherit from World"), int(iris::CameraExposureMode::Inherit));
        mode->addItem(QStringLiteral("Auto (this camera)"), int(iris::CameraExposureMode::Auto));
        mode->addItem(QStringLiteral("Manual"),             int(iris::CameraExposureMode::Manual));
        mode->setCurrentIndex(int(camera->exposureMode));
        mode->setToolTip(QStringLiteral(
            "How THIS camera is exposed while it is the one you are looking through — "
            "piloting it, playing through it, or shooting it with camera.screenshot. "
            "Inherit leaves the world's exposure exactly as it is. Auto gives the camera its "
            "own adaptation midpoint and window; Manual pins the grade so it measures nothing. "
            "Never applies to thumbnails, previews or renders that asked for a neutral readback."));
        connect(mode, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this,
                [this, mode](int row) {
                    if (!camera || row < 0) return;
                    camera->setPropertyValue(QStringLiteral("exposureMode"),
                                             mode->getItemData(row).toInt());
                    applied(true);   // the window rows below become (ir)relevant
                });

        const bool own = camera->exposureMode != iris::CameraExposureMode::Inherit;
        auto *stops = this->addDragFloat(QStringLiteral("Exposure (stops)"),
                                         double(camera->exposure), -20.0, 20.0, 0.01, 2);
        stops->setEnabled(own);
        stops->setToolTip(QStringLiteral(
            "The camera's exposure in STOPS: 0 is the default world grade and +1 is one "
            "doubling. This is not the World panel's Exposure number, which is the post "
            "chain's own natural-log value — one stop is ln 2 of it, converted for you."));
        connect(stops, &DragFloatWidget::valueChanged, this, [this](double v) {
            if (!camera) return;
            camera->setPropertyValue(QStringLiteral("exposure"), float(v));
            applied(false);   // no rebuild: this fires on every scrubbed pixel
        });

        const bool autoMode = camera->exposureMode == iris::CameraExposureMode::Auto;
        for (const char *which : { "exposureMin", "exposureMax" }) {
            const QString key = QLatin1String(which);
            const bool isMin = key == QLatin1String("exposureMin");
            auto *field = this->addDragFloat(isMin ? QStringLiteral("Auto Min (stops)")
                                                   : QStringLiteral("Auto Max (stops)"),
                                             double(isMin ? camera->exposureMin : camera->exposureMax),
                                             -20.0, 20.0, 0.01, 2);
            field->setEnabled(autoMode);
            field->setToolTip(QStringLiteral(
                "The window automatic exposure may adapt within, in stops. Ignored in Manual "
                "mode, which pins the grade outright."));
            connect(field, &DragFloatWidget::valueChanged, this, [this, key](double v) {
                if (!camera) return;
                camera->setPropertyValue(key, float(v));
                applied(false);
            });
        }
    }

    // ---- §5, the tri-state overrides --------------------------------------
    // Generated: the DOCUMENT's key table decides what exists, the World tables
    // decide what it is called, what it costs and whether it is served.
    int count = 0;
    const iris::CameraPostKey *table = iris::cameraPostKeys(count);
    for (int i = 0; i < count; ++i) {
        const QString key = QString::fromLatin1(table[i].id);
        const QString label = labelFor(key);
        const QVariant own = camera->postOverride(key);

        if (!rowAvailable(key)) {
            // Shown so the section is a complete account of the chain, disabled
            // so it cannot be pinned to something the renderer would ignore —
            // the same choice the World section makes.
            auto *lbl = this->addLabel(label, QStringLiteral("not available yet"));
            if (lbl) lbl->setToolTip(tooltipFor(key));
            continue;
        }

        if (table[i].type == iris::CameraPostKeyType::Number) {
            // OVERRIDE + VALUE. The checkbox is the tri-state; the number is
            // greyed while it inherits and shows the world's value there, so
            // "what would this be" never needs a trip to the World panel.
            auto *box = this->addCheckBox(QStringLiteral("Override %1").arg(label),
                                          own.isValid());
            box->setValue(own.isValid());
            box->setToolTip(tooltipFor(key));
            connect(box, &CheckBoxWidget::valueChanged, this, [this, key](bool on) {
                if (!camera) return;
                if (on) {
                    // Start from what it was already inheriting: turning an
                    // override on must not change the picture by itself.
                    const worldmodes::ParamRow *p = worldmodes::postFxParam(key);
                    const iris::ScenePtr scene = sceneOf(camera);
                    const double seed = (p && p->get && scene) ? p->get(scene) : 0.0;
                    camera->setPostOverride(key, seed);
                } else {
                    camera->clearPostOverride(key);
                }
                applied(true);
            });

            const worldmodes::ParamRow *p = worldmodes::postFxParam(key);
            const double minV = p ? p->minValue : 0.0, maxV = p ? p->maxValue : 1.0;
            const double step = p ? p->perPixelStep : 0.02;
            const int decimals = p ? p->decimals : 2;
            const iris::ScenePtr scene = sceneOf(camera);
            const double shown = own.isValid()
                                     ? own.toDouble()
                                     : ((p && p->get && scene) ? p->get(scene) : 0.0);
            auto *field = this->addDragFloat(label, shown, minV, maxV, step, decimals);
            field->setEnabled(own.isValid());
            field->setToolTip(tooltipFor(key));
            connect(field, &DragFloatWidget::valueChanged, this, [this, key, minV, maxV](double v) {
                if (!camera) return;
                camera->setPostOverride(key, qBound(minV, v, maxV));
                applied(false);
            });
            continue;
        }

        // TOGGLE and ENUM rows are one combo whose FIRST item is "inherit" —
        // the only shape that lets a user pick inheritance rather than fall
        // into it. The inherited value is spelled out in the item text.
        auto *combo = this->addComboBox(label);
        combo->addItem(QStringLiteral("Inherit (%1)").arg(inheritedText(key)), QVariant());
        if (table[i].type == iris::CameraPostKeyType::Toggle) {
            combo->addItem(QStringLiteral("On"), 1);
            combo->addItem(QStringLiteral("Off"), 0);
            combo->setCurrentIndex(!own.isValid() ? 0 : (own.toInt() != 0 ? 1 : 2));
        } else if (key == QLatin1String("smaa")) {
            // A camera may switch SMAA OFF (compositor shape, free) but not
            // pick a PRESET: that is a shader recompile and would hitch on
            // every cut. Recorded here in the tooltip, and refused by the
            // document, so the two cannot drift apart.
            combo->addItem(QStringLiteral("Off"), -1);
            combo->setCurrentIndex(own.isValid() ? 1 : 0);
            combo->setToolTip(QStringLiteral(
                "A camera can turn SMAA off, but the PRESET (and MSAA) stay world-level: "
                "both are shader recompiles, so a per-camera value would hitch on every cut."));
        } else {
            const worldmodes::Row *r = worldmodes::row(key);
            int current = 0;
            if (r) {
                for (int o = 0; o < r->options.size(); ++o) {
                    combo->addItem(r->options[o].label, r->options[o].value);
                    if (own.isValid() && r->options[o].value == own.toInt()) current = o + 1;
                }
            }
            combo->setCurrentIndex(current);
        }
        if (combo->toolTip().isEmpty()) combo->setToolTip(tooltipFor(key));
        connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this,
                [this, key, combo](int row) {
                    if (!camera || row < 0) return;
                    const QVariant data = combo->getItemData(row);
                    if (!data.isValid() || data.isNull()) camera->clearPostOverride(key);
                    else                                  camera->setPostOverride(key, data);
                    applied(true);
                });
    }

    // PiP HONESTY (CAMERA_LENS_SPEC §7). The selection inset has no post chain
    // at all — a fill swatch and one bare scene pass (OgreChain.cpp buildPip) —
    // so nothing on this panel changes what it shows. Saying so costs one line
    // and saves the "why does the preview not update" question; the fix is
    // CAMERAS_SPEC §7.2's Route C, which is deferred.
    if (auto *hint = this->addLabel(QStringLiteral("Preview inset"),
                                    QStringLiteral("shows the world's look")))
        hint->setToolTip(QStringLiteral(
            "The picture-in-picture inset renders without a post chain, so a camera's own "
            "exposure and overrides do not appear in it. Pilot the camera (or play through "
            "it) to see the graded shot in the main viewport."));
}

void CameraPostFxPropertyWidget::applied(bool rebuildPanel)
{
    // Two frames, like every sibling section: an enable-flag change is a
    // workspace rebuild applied by SceneMirror at the next sync, and a single
    // frame can be the one that rebuilds rather than the one that draws with it.
    if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
    if (rebuildPanel) rebuild();
}
