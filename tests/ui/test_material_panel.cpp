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

#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/core/properties/property.h"

#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "services/services.h"
#include "services/undoservice.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); } \
    else { printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); ++failures; } \
} while (0)

namespace {

int propId(const iris::MaterialPtr &mat, const QString &name)
{
    for (auto *p : mat->properties)
        if (p->name == name) return p->id;
    return -1;
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
    CHECK(textures.size() >= 6, "texture: six map rows exist");
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

// The Alpha Mode row is an IntProperty rendered as a labeled dropdown (the
// Unreal-parity blend modes; combo index == stored alphaMode value). It was
// once a dead slider row — this guards both the wiring and the enum labels.
static void testIntRow()
{
    PanelRig rig;
    ComboBoxWidget *row = nullptr;
    for (auto *w : rig.panel.findChildren<ComboBoxWidget *>())
        if (w->index == propId(rig.pbr, "alphaMode")) { row = w; break; }
    CHECK(row != nullptr, "int: alphaMode row exists as a dropdown");
    if (!row) return;

    auto *combo = row->getWidget();
    CHECK(combo->count() == 6, "int: six blend modes listed");
    CHECK(combo->itemText(4) == "Additive" && combo->itemText(5) == "Modulate",
          "int: Additive/Modulate entries present");

    const int before = rig.stack.count();
    combo->setCurrentIndex(4);   // Additive
    CHECK(rig.pbr->alphaMode == 4, "int: field updates on pick");
    CHECK(rig.stack.count() == before + 1, "int: one undo entry per pick");

    rig.undo.undo();
    CHECK(rig.pbr->alphaMode == 0, "int: undo restores the field");
    rig.undo.redo();
    CHECK(rig.pbr->alphaMode == 4, "int: redo reapplies the field");
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
        { "normalFactor", 1.0f }, { "occlusionFactor", 1.0f }, { "alpha", 1.0f },
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
    const float poison = 1.0f;
    for (size_t i = 0; i + sizeof(float) <= sizeof(HFloatSliderWidget); i += sizeof(float))
        memcpy(static_cast<char *>(mem) + i, &poison, sizeof(float));

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

    printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
