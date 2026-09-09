/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.material_panel — the editor's material properties panel actually edits
// the material (owner-reported regression, 2026-08-31).
//
// The bug class this guards: MaterialPropertyWidget rendered a generic
// material's (PbrMaterial's) Property list and wrote edits back into the LIST
// only. But what renders are the material's FIELDS (SceneMirror::toPbrParams
// reads pbr->textureScale etc.), which only Material::setValue updates. Edits
// looked dead live and only surfaced after a scene reload rebuilt the material
// from JSON through setValue. This test drives the real widgets - the same
// HFloatSliderWidget gestures a user makes - and asserts on the FIELDS,
// plus the one-undo-entry-per-gesture contract.

#include <QApplication>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QUndoStack>
#include <QTest>
#include <QTemporaryDir>
#include <QImage>

#include <cstring>
#include <functional>
#include <new>

#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/core/properties/property.h"

#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/checkboxwidget.h"
#include "services/services.h"
#include "services/undoservice.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); } \
    else { printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); ++failures; } \
} while (0)

namespace {

iris::Property *findProp(const iris::MaterialPtr &mat, const QString &name)
{
    for (auto *p : mat->properties)
        if (p->name == name) return p;
    return nullptr;
}

int propId(const iris::MaterialPtr &mat, const QString &name)
{
    auto *p = findProp(mat, name);
    return p ? p->id : -1;
}

HFloatSliderWidget *sliderRow(QWidget *panel, int id)
{
    for (auto *w : panel->findChildren<HFloatSliderWidget *>())
        if (w->index == id) return w;
    return nullptr;
}

struct PanelRig {
    QUndoStack stack;
    UndoService undo{&stack};
    StudioServices services;
    iris::MeshNodePtr node;
    QSharedPointer<iris::PbrMaterial> pbr;
    MaterialPropertyWidget panel;

    // `configure` runs on the material BEFORE the panel is built, so a rig can
    // model a real-world material (e.g. an image plane's roughness 1 /
    // metallic 0) instead of the PbrMaterial defaults.
    explicit PanelRig(const std::function<void(const QSharedPointer<iris::PbrMaterial> &)> &configure = {})
    {
        services.undo = &undo;
        pbr = iris::PbrMaterial::create();
        if (configure) configure(pbr);
        node = iris::MeshNode::create();
        node->setMaterial(pbr);
        panel.setServices(&services);
        // no database / project: the generic-material path must not need them
        panel.setDatabase(nullptr);
        panel.setSceneNode(node);
        panel.show();
    }
};

} // namespace

// A slider drag: press, move (live preview must reach the FIELD the mirror
// renders from), release (exactly one undo entry), undo (field reverts).
static void testSliderDragLive()
{
    PanelRig rig;
    auto *row = sliderRow(&rig.panel, propId(rig.pbr, "textureScale"));
    CHECK(row != nullptr, "slider: textureScale row exists");
    if (!row) return;

    auto *slider = row->findChild<QSlider *>();
    CHECK(slider != nullptr, "slider: QSlider child found");
    if (!slider) return;

    const int before = rig.stack.count();
    slider->setSliderDown(true);           // emits sliderPressed -> changeStart
    slider->setValue(800);                 // textureScale range 0..10 -> 8.0
    // live preview DURING the drag: the field, not just the Property object
    CHECK(qAbs(rig.pbr->textureScale - 8.0f) < 1e-3f, "slider: field updates live mid-drag");
    slider->setValue(600);                 // keep dragging -> 6.0
    CHECK(qAbs(rig.pbr->textureScale - 6.0f) < 1e-3f, "slider: field follows the drag");
    CHECK(rig.stack.count() == before, "slider: no undo entries while dragging");
    slider->setSliderDown(false);          // emits sliderReleased -> changeEnd
    CHECK(rig.stack.count() == before + 1, "slider: exactly one undo entry on release");

    rig.undo.undo();
    CHECK(qAbs(rig.pbr->textureScale - 1.0f) < 1e-3f, "slider: undo restores the field");
    rig.undo.redo();
    CHECK(qAbs(rig.pbr->textureScale - 6.0f) < 1e-3f, "slider: redo reapplies the field");
}

// Typing a value into the spinbox: the value must reach the field as it is
// typed and commit one undo entry on Enter.
static void testTypedEntry()
{
    PanelRig rig;
    auto *row = sliderRow(&rig.panel, propId(rig.pbr, "textureScale"));
    CHECK(row != nullptr, "typed: textureScale row exists");
    if (!row) return;

    auto *box = row->findChild<QDoubleSpinBox *>();
    CHECK(box != nullptr, "typed: spinbox child found");
    if (!box) return;

    QTest::qWaitForWindowExposed(&rig.panel);
    rig.panel.activateWindow();
    QTest::qWaitForWindowActive(&rig.panel, 1000);
    box->setFocus();
    QApplication::processEvents();
    if (!box->hasFocus()) {
        // offscreen platforms without focus support can't drive this path
        printf("SKIP typed: no focus support on this platform\n");
        return;
    }

    const int before = rig.stack.count();
    box->selectAll();
    QTest::keyClicks(box, "7");
    CHECK(qAbs(rig.pbr->textureScale - 7.0f) < 1e-3f, "typed: field updates while typing");
    CHECK(rig.stack.count() == before, "typed: no undo entry before Enter");
    QTest::keyClick(box, Qt::Key_Return);
    CHECK(rig.stack.count() == before + 1, "typed: one undo entry on Enter");

    rig.undo.undo();
    CHECK(qAbs(rig.pbr->textureScale - 1.0f) < 1e-3f, "typed: undo restores the field");
}

// The colour rows: live preview onto the field during the pick, one undo entry
// per popup session.
static void testColorPickSession()
{
    PanelRig rig;
    auto pickers = rig.panel.findChildren<ColorValueWidget *>();
    CHECK(pickers.size() >= 2, "color: baseColor+emissiveColor rows exist");
    if (pickers.isEmpty()) return;

    ColorPickerWidget *picker = pickers.first()->getPicker();  // Base Color
    CHECK(picker != nullptr, "color: picker exists");
    if (!picker) return;

    const int before = rig.stack.count();
    QMetaObject::invokeMethod(picker, "pickingStarted");     // popup opened
    QMetaObject::invokeMethod(picker, "colorChanged",        // live change
                              Q_ARG(QColor, QColor(255, 0, 0)));
    CHECK(rig.pbr->baseColor == QColor(255, 0, 0), "color: field updates live");
    CHECK(rig.stack.count() == before, "color: no undo entry mid-pick");
    QMetaObject::invokeMethod(picker, "pickingEnded");       // popup closed
    CHECK(rig.stack.count() == before + 1, "color: one undo entry per pick session");

    rig.undo.undo();
    CHECK(rig.pbr->baseColor == QColor(255, 255, 255), "color: undo restores the field");
}

// A cancelled gesture (opened and closed with no change) must not pollute the
// undo stack.
static void testNoOpGestureNoUndo()
{
    PanelRig rig;
    auto pickers = rig.panel.findChildren<ColorValueWidget *>();
    if (pickers.isEmpty()) { CHECK(false, "noop: color row exists"); return; }
    ColorPickerWidget *picker = pickers.first()->getPicker();

    const int before = rig.stack.count();
    QMetaObject::invokeMethod(picker, "pickingStarted");
    QMetaObject::invokeMethod(picker, "pickingEnded");
    CHECK(rig.stack.count() == before, "noop: unchanged pick pushes no undo entry");
}

// Texture rows: choosing a map must load it onto the material (the textures
// map the mirror binds from), as one undo entry; undo clears it again.
static void testTextureRow()
{
    QTemporaryDir dir;
    const QString imgPath = dir.filePath("map.png");
    QImage img(4, 4, QImage::Format_RGBA8888);
    img.fill(Qt::green);
    img.save(imgPath);

    PanelRig rig;
    auto textures = rig.panel.findChildren<TexturePickerWidget *>();
    // FIVE, not six: HLMS_ADOPTION P2 removed the Occlusion Map row along with
    // the rest of the AO ghost (the renderer has no AO input to bind it to).
    CHECK(textures.size() == 5, "texture: five map rows exist (no Occlusion Map)");
    if (textures.isEmpty()) return;

    // rows appear in property order; the first texture property is baseColorMap
    TexturePickerWidget *baseMap = textures.first();
    const int before = rig.stack.count();
    QMetaObject::invokeMethod(baseMap, "valueChanged", Q_ARG(QString, imgPath));
    CHECK(rig.pbr->textures.contains("u_baseColorMap"), "texture: map lands on the material");
    CHECK(rig.stack.count() == before + 1, "texture: one undo entry per pick");

    rig.undo.undo();
    CHECK(!rig.pbr->textures.contains("u_baseColorMap"), "texture: undo clears the map");
}

// The Alpha Mode row is an ENUM row (iris::ListProperty) rendered as a labeled
// dropdown: combo index == the stored alphaMode value, and the labels ride the
// PROPERTY, not a by-name branch in the panel (HLMS_ADOPTION P1 generalised
// what used to be `if (name == "alphaMode")`). It was once a dead slider row —
// this guards the wiring, the labels and the generic mechanism.
static void testIntRow()
{
    PanelRig rig;
    ComboBoxWidget *row = nullptr;
    for (auto *w : rig.panel.findChildren<ComboBoxWidget *>())
        if (w->index == propId(rig.pbr, "alphaMode")) { row = w; break; }
    CHECK(row != nullptr, "int: alphaMode row exists as a dropdown");
    if (!row) return;

    auto *combo = row->getWidget();
    // The combo must cover the property's WHOLE vocabulary. A mode with no
    // entry (Refractive was missing one — PUBLISH_AUDIT #4) showed a blank
    // combo and silently downgraded the material on the next pick. Since the
    // labels now LIVE on the property, this compares against them rather than
    // against a declared range the panel could disagree with — the mechanism
    // is what makes the two unable to drift apart, and this is its assertion.
    const auto *alphaProp = static_cast<const iris::ListProperty *>(
        findProp(rig.pbr, "alphaMode"));
    CHECK(alphaProp && alphaProp->type == iris::PropertyType::List,
          "int: alphaMode is the generic enum row, not a by-name special case");
    CHECK(alphaProp && combo->count() == alphaProp->labels.size(),
          "int: one combo entry per declared alpha mode label");
    CHECK(combo->itemText(4) == "Additive" && combo->itemText(5) == "Modulate",
          "int: Additive/Modulate entries present");
    CHECK(combo->itemText(6) == "Refractive", "int: Refractive entry present");

    const int before = rig.stack.count();
    combo->setCurrentIndex(4);   // Additive
    CHECK(rig.pbr->alphaMode == 4, "int: field updates on pick");
    CHECK(rig.stack.count() == before + 1, "int: one undo entry per pick");

    rig.undo.undo();
    CHECK(rig.pbr->alphaMode == 0, "int: undo restores the field");
    rig.undo.redo();
    CHECK(rig.pbr->alphaMode == 4, "int: redo reapplies the field");

    // A refractive pick must SURVIVE. With no entry for it the combo simply had
    // no index 6 to select, so the mode was unreachable from the panel and any
    // pick on a refractive material silently downgraded it (PUBLISH_AUDIT #4).
    combo->setCurrentIndex(6);
    CHECK(rig.pbr->alphaMode == 6, "int: Refractive is selectable and lands on the material");
    rig.undo.undo();
    CHECK(rig.pbr->alphaMode == 4, "int: undo restores the pre-Refractive mode");
}

// Every float/int row must DISPLAY the material's value the moment the panel
// is built. Owner-visible symptom (IMAGE_PLANE_SPEC follow-up, 2026-08-31): a
// dropped image plane's Roughness row read a value the material never had,
// while the same row on another mesh read correctly.
static void testRowsDisplayTheMaterialValues()
{
    // An image plane's material: roughness 1, metallic 0 (ImageMaterial::fromTexture).
    PanelRig rig([](const QSharedPointer<iris::PbrMaterial> &pbr) {
        pbr->setValue("roughness", 1.0f);
        pbr->setValue("metallic", 0.0f);
    });

    struct { const char *prop; float expected; } rows[] = {
        { "roughness", 1.0f }, { "metallic", 0.0f }, { "textureScale", 1.0f },
        { "normalFactor", 1.0f }, { "alpha", 1.0f },
    };
    for (const auto &r : rows) {
        auto *row = sliderRow(&rig.panel, propId(rig.pbr, r.prop));
        auto *box = row ? row->findChild<QDoubleSpinBox *>() : nullptr;
        CHECK(box && qAbs(float(box->value()) - r.expected) < 1e-3f,
              qPrintable(QStringLiteral("display: %1 row shows %2 (spinbox %3)")
                             .arg(r.prop).arg(r.expected)
                             .arg(box ? box->value() : -1.0)));
    }
}

// The mechanism behind the row above: a row is built fresh for every selection
// and its `value` member used to start as whatever the allocator handed back
// (an uninitialized read). setValue() then early-returned when that stale
// float already equalled the material's value, leaving the spinbox at its .ui
// default of 0.00 - a wrong number on screen for a correct material. Panel
// rebuilds recycle the previous panel's blocks, so "the stale float equals the
// new value" is the common case, not a rare one (select two meshes whose
// roughness matches). Deterministic here: build the row over memory that
// already carries 1.0f in every float-sized slot.
static void testStaleRowMemoryStillDisplays()
{
    void *mem = ::operator new(sizeof(HFloatSliderWidget));
    const float fillPattern = 1.0f;
    for (size_t i = 0; i + sizeof(float) <= sizeof(HFloatSliderWidget); i += sizeof(float))
        memcpy(static_cast<char *>(mem) + i, &fillPattern, sizeof(float));

    auto *row = new (mem) HFloatSliderWidget();
    row->setRange(0.0f, 1.0f);          // exactly what addFloatValueSlider does
    row->setValue(1.0f);                // ... and what addFloatProperty does

    auto *box = row->findChild<QDoubleSpinBox *>();
    CHECK(box && qAbs(box->value() - 1.0) < 1e-3,
          "stale memory: spinbox shows the value, not the .ui default");
    auto *slider = row->findChild<QSlider *>();
    CHECK(slider && slider->value() == slider->maximum(),
          "stale memory: slider tracks the value");
    CHECK(qAbs(row->getValue() - 1.0f) < 1e-3f, "stale memory: getValue agrees");

    row->~HFloatSliderWidget();
    ::operator delete(mem);
}

// The same thing through the real allocator: selecting a second mesh rebuilds
// the panel, and the new rows land in the freed blocks of the old ones.
static void testPanelRebuildDisplays()
{
    auto imagePlaneMaterial = [](const QSharedPointer<iris::PbrMaterial> &pbr) {
        pbr->setValue("roughness", 1.0f);
    };
    {
        PanelRig first(imagePlaneMaterial);   // rows freed at scope exit
        (void)first;
    }
    PanelRig second(imagePlaneMaterial);
    auto *row = sliderRow(&second.panel, propId(second.pbr, "roughness"));
    auto *box = row ? row->findChild<QDoubleSpinBox *>() : nullptr;
    CHECK(box && qAbs(box->value() - 1.0) < 1e-3,
          "rebuild: the second panel's Roughness row still shows 1.00");
}

// HLMS_ADOPTION P1: the BRDF picker is the SECOND user of the generic enum row
// (its whole reason for existing), and it CONSTRAINS the two clear-coat rows.
//
// The renderer cannot carry a clear coat on anything but the Default BRDF
// family, and the decided behaviour (D-P1b) is that the panel DISABLES the coat
// rows rather than clearing them: a BRDF experiment must be reversible, so the
// authored coat has to survive a trip through Cook-Torrance and come back. A
// row that is editable but silently does nothing is the defect class this whole
// program exists to remove — so "greyed out, with a reason in the tooltip" is
// the assertion, not "the value got zeroed".
static void testBrdfRowAndClearCoatConstraint()
{
    PanelRig rig;

    ComboBoxWidget *brdfRow = nullptr;
    for (auto *w : rig.panel.findChildren<ComboBoxWidget *>())
        if (w->index == propId(rig.pbr, "brdf")) { brdfRow = w; break; }
    CHECK(brdfRow != nullptr, "brdf: the picker exists as a dropdown");
    if (!brdfRow) return;

    const auto *brdfProp = static_cast<const iris::ListProperty *>(findProp(rig.pbr, "brdf"));
    auto *combo = brdfRow->getWidget();
    CHECK(brdfProp && combo->count() == brdfProp->labels.size(),
          "brdf: one combo entry per declared BRDF label");
    CHECK(combo->count() == 6, "brdf: the curated six, not all twelve of the renderer's");
    CHECK(combo->itemText(0) == "Default", "brdf: entry 0 is Default");

    auto *coat      = sliderRow(&rig.panel, propId(rig.pbr, "clearCoat"));
    auto *coatRough = sliderRow(&rig.panel, propId(rig.pbr, "clearCoatRoughness"));
    CHECK(coat && coatRough, "coat: both clear-coat rows exist");
    if (!coat || !coatRough) return;
    CHECK(coat->isEnabled() && coatRough->isEnabled(),
          "coat: rows are live on the Default BRDF");

    // author a coat, then move to Cook-Torrance (index 1, a different family)
    auto *coatSlider = coat->findChild<QSlider *>();
    CHECK(coatSlider != nullptr, "coat: slider child found");
    if (!coatSlider) return;
    coatSlider->setSliderDown(true);
    coatSlider->setValue(700);            // clearCoat range 0..1 -> 0.7
    coatSlider->setSliderDown(false);
    CHECK(qAbs(rig.pbr->clearCoat - 0.7f) < 1e-3f, "coat: the slider reaches the field");

    combo->setCurrentIndex(1);            // Cook-Torrance
    CHECK(rig.pbr->brdf == 1, "brdf: the pick reaches the field");
    CHECK(!coat->isEnabled() && !coatRough->isEnabled(),
          "coat: both rows DISABLE on a non-Default BRDF");
    CHECK(!coat->toolTip().isEmpty(),
          "coat: the disabled row says WHY (a greyed row with no reason is a dead row)");
    CHECK(qAbs(rig.pbr->clearCoat - 0.7f) < 1e-3f,
          "coat: the authored value is KEPT, not zeroed, while the BRDF hides it");

    // index 3 is DefaultSeparateDiffuseFresnel — still the Default FAMILY, so
    // the coat comes back. This is the case a naive "index == 0" rule breaks.
    combo->setCurrentIndex(3);
    CHECK(coat->isEnabled() && coatRough->isEnabled(),
          "coat: rows live again on a Default-FAMILY variant (index 3), not just index 0");
    CHECK(qAbs(rig.pbr->clearCoat - 0.7f) < 1e-3f, "coat: the value survived the round trip");

    rig.undo.undo();
    CHECK(rig.pbr->brdf == 1, "brdf: undo steps back one pick");
}

// HLMS_ADOPTION P4a: the Shading Model row constrains most of the panel.
//
// Unlit is the OTHER renderer family, with no lighting term at all, so
// thirteen rows below it have nothing to reach. The same rule as the clear
// coat applies and for the same reason: DISABLE with a stated reason, never
// clear — the values stay authored and come back when the model does. The
// coverage that matters here is that the constraint reaches EVERY KIND of row
// (float, enum, bool, colour, texture), because until this phase only float
// and enum rows were even findable by name.
static void testShadingModelRowConstraints()
{
    PanelRig rig;

    ComboBoxWidget *modelRow = nullptr;
    for (auto *w : rig.panel.findChildren<ComboBoxWidget *>())
        if (w->index == propId(rig.pbr, "shadingModel")) { modelRow = w; break; }
    CHECK(modelRow != nullptr, "shadingModel: the picker exists as a dropdown");
    if (!modelRow) return;
    auto *combo = modelRow->getWidget();
    // THREE models since POST_LOOKS_SPEC §5.2: Distortion joined Lit and Unlit.
    CHECK(combo->count() == 3, "shadingModel: three models");
    CHECK(combo->itemText(0) == "Lit" && combo->itemText(1) == "Unlit" &&
              combo->itemText(2) == "Distortion",
          "shadingModel: Lit, Unlit, Distortion, in that order (the index is the stored value)");

    // One representative of each row KIND, all in rowsUnusedWhenUnlit().
    auto *roughness = sliderRow(&rig.panel, propId(rig.pbr, "roughness"));      // float
    ComboBoxWidget *brdf = nullptr;                                             // enum
    for (auto *w : rig.panel.findChildren<ComboBoxWidget *>())
        if (w->index == propId(rig.pbr, "brdf")) { brdf = w; break; }
    CheckBoxWidget *receive = nullptr;                                          // bool
    for (auto *w : rig.panel.findChildren<CheckBoxWidget *>())
        if (w->index == propId(rig.pbr, "receiveShadows")) { receive = w; break; }
    ColorValueWidget *emissive = nullptr;                                       // colour
    for (auto *w : rig.panel.findChildren<ColorValueWidget *>())
        if (w->index == propId(rig.pbr, "emissiveColor")) { emissive = w; break; }
    TexturePickerWidget *normalMap = nullptr;                                   // texture
    for (auto *w : rig.panel.findChildren<TexturePickerWidget *>())
        if (w->index == propId(rig.pbr, "normalMap")) { normalMap = w; break; }
    CHECK(roughness && brdf && receive && emissive && normalMap,
          "shadingModel: one row of every kind was found by name");
    if (!roughness || !brdf || !receive || !emissive || !normalMap) return;

    // A row the model does NOT constrain — the control. Base colour is the one
    // thing Unlit renders, so it must stay live.
    ColorValueWidget *baseColor = nullptr;
    for (auto *w : rig.panel.findChildren<ColorValueWidget *>())
        if (w->index == propId(rig.pbr, "baseColor")) { baseColor = w; break; }
    CHECK(baseColor != nullptr, "shadingModel: the base colour row exists");

    CHECK(roughness->isEnabled() && brdf->isEnabled() && receive->isEnabled() &&
          emissive->isEnabled() && normalMap->isEnabled(),
          "shadingModel: everything is live on the Lit model");

    // author a roughness the switch must not destroy
    auto *roughSlider = roughness->findChild<QSlider *>();
    CHECK(roughSlider != nullptr, "shadingModel: roughness slider child found");
    if (!roughSlider) return;
    roughSlider->setSliderDown(true);
    roughSlider->setValue(230);          // roughness range 0..1 -> 0.23
    roughSlider->setSliderDown(false);
    CHECK(qAbs(rig.pbr->roughnessFactor - 0.23f) < 1e-3f, "shadingModel: roughness authored");

    combo->setCurrentIndex(1);           // Unlit
    CHECK(rig.pbr->shadingModel == 1, "shadingModel: the pick reaches the field");
    CHECK(!roughness->isEnabled() && !brdf->isEnabled() && !receive->isEnabled() &&
          !emissive->isEnabled() && !normalMap->isEnabled(),
          "shadingModel: every kind of unusable row DISABLES on Unlit");
    CHECK(!roughness->toolTip().isEmpty() && !normalMap->toolTip().isEmpty(),
          "shadingModel: the disabled rows say WHY");
    CHECK(baseColor && baseColor->isEnabled(),
          "shadingModel: base colour stays live — it is what Unlit renders");
    CHECK(qAbs(rig.pbr->roughnessFactor - 0.23f) < 1e-3f,
          "shadingModel: the authored roughness is KEPT, not zeroed");

    // DISTORTION greys far more than Unlit (POST_LOOKS_SPEC §5.2): the object
    // draws no surface at all, so even the base colour — the one thing Unlit
    // renders — goes. What is LEFT is the authoring surface: the normal map
    // (read as the screen-space displacement field) and the opacity (its own
    // strength). Asserting the survivors matters more than asserting the
    // casualties: a model that greyed everything would pass a "these are
    // disabled" test and be unusable.
    combo->setCurrentIndex(2);           // Distortion
    CHECK(rig.pbr->shadingModel == 2, "shadingModel: Distortion reaches the field");
    CHECK(normalMap->isEnabled(),
          "shadingModel: the normal map STAYS live on Distortion — it is the "
          "displacement field, and reusing that picker is why this is a shading "
          "model and not a new node type");
    CHECK(baseColor && !baseColor->isEnabled(),
          "shadingModel: base colour goes on Distortion (unlike Unlit)");
    CHECK(!roughness->isEnabled() && !brdf->isEnabled() && !emissive->isEnabled(),
          "shadingModel: the PBR rows are gone too");
    CHECK(!baseColor->toolTip().isEmpty(),
          "shadingModel: and the disabled rows say WHY, in Distortion's own words");
    CHECK(qAbs(rig.pbr->roughnessFactor - 0.23f) < 1e-3f,
          "shadingModel: the authored roughness survives Distortion as well");

    combo->setCurrentIndex(0);           // back to Lit
    CHECK(roughness->isEnabled() && brdf->isEnabled() && receive->isEnabled() &&
          emissive->isEnabled() && normalMap->isEnabled(),
          "shadingModel: every row comes back with the Lit model");
    CHECK(qAbs(rig.pbr->roughnessFactor - 0.23f) < 1e-3f,
          "shadingModel: and the value is still there");

    rig.undo.undo();
    CHECK(rig.pbr->shadingModel == 2, "shadingModel: undo steps back one pick");
    rig.undo.redo();
    CHECK(rig.pbr->shadingModel == 0, "shadingModel: redo re-applies it");
}

// THE BLADE-REUSE SNAPSHOT (hygiene lane, 2026-09-09).
//
// `existingTextures` is what updateTextureDependency reads to find the project
// dependency an EMPTIED texture row used to point at. It was only ever inserted
// into. The properties panel keeps its blades as hidden children and reuses
// them across selections (the selection-stall fix), so one widget sees every
// mesh the user clicks — and selecting anything that is NOT a mesh (a light, a
// camera, an empty) takes setSceneNode's early return, which left the previous
// mesh's texture paths behind. The panel then described a material it was no
// longer showing, and the next texture edit deleted a dependency belonging to a
// different object.
static void testTextureSnapshotDoesNotAccumulate()
{
    QTemporaryDir dir;
    const QString imgPath = dir.filePath("snapshot.png");
    QImage img(4, 4, QImage::Format_RGBA8888);
    img.fill(Qt::magenta);
    img.save(imgPath);

    PanelRig rig([&](const QSharedPointer<iris::PbrMaterial> &m) {
        m->setValue(QStringLiteral("baseColorMap"), imgPath);
    });
    CHECK(rig.panel.shownTextures().value("baseColorMap") == imgPath,
          "snapshot: the shown material's texture row is recorded");

    // A NON-MESH selection: nothing is shown, so nothing may be remembered.
    auto light = iris::SceneNode::create();          // an Empty, not a mesh
    rig.panel.setSceneNode(light);
    CHECK(rig.panel.shownTextures().isEmpty(),
          "snapshot: selecting a non-mesh node forgets the previous mesh's maps");

    // ...and a mesh whose material carries no maps leaves nothing behind either.
    auto plain = iris::PbrMaterial::create();
    auto second = iris::MeshNode::create();
    second->setMaterial(plain);
    rig.panel.setSceneNode(second);
    for (auto it = rig.panel.shownTextures().constBegin();
         it != rig.panel.shownTextures().constEnd(); ++it)
        CHECK(it.value() != imgPath, "snapshot: no row anywhere still holds the old path");

    // A MESH WITH NO MATERIAL AT ALL is the same statement: nothing shown,
    // nothing remembered. (setSceneNode returns early here too.)
    rig.panel.setSceneNode(second);
    auto bare = iris::MeshNode::create();
    rig.panel.setSceneNode(bare);
    CHECK(rig.panel.shownTextures().isEmpty(),
          "snapshot: a mesh with no material leaves an empty snapshot");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    testRowsDisplayTheMaterialValues();
    testPanelRebuildDisplays();
    testStaleRowMemoryStillDisplays();
    testSliderDragLive();
    testTypedEntry();
    testColorPickSession();
    testNoOpGestureNoUndo();
    testTextureRow();
    testIntRow();
    testBrdfRowAndClearCoatConstraint();
    testShadingModelRowConstraints();
    testTextureSnapshotDoesNotAccumulate();

    printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
