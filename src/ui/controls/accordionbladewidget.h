/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ACCORDIANBLADEWIDGET_H
#define ACCORDIANBLADEWIDGET_H

#include <QWidget>

namespace Ui {
    class AccordianBladeWidget;
}

class TransformEditor;
class ColorValueWidget;
class TexturePickerWidget;
class HFloatSliderWidget;
class CheckBoxWidget;
class ComboBoxWidget;
class TextInputWidget;
class LabelWidget;
class FilePickerWidget;
class CubeMapWidget;
class DragFloatWidget;
class DragVector3Widget;
// class PropertyWidget;
#include "ui/panels/propertywidget.h"
#include "modules/materials/propertywidgets/propertywidgetbase.h"

#include <QLayout>
#include <QPointer>

class Project;

class AccordianBladeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AccordianBladeWidget(QWidget* parent = 0);
    ~AccordianBladeWidget();

    TransformEditor*        addTransformControls();

    ColorValueWidget*       addColorPicker(const QString&);
    TexturePickerWidget*    addTexturePicker(const QString&);
    HFloatSliderWidget*     addFloatValueSlider(const QString&, float start, float end, float value = 0.f);
    CheckBoxWidget*         addCheckBox(const QString&, bool value = false);
    ComboBoxWidget*         addComboBox(const QString&);
    TextInputWidget*        addTextInput(const QString&);
    LabelWidget*            addLabel(const QString&, const QString&);
    FilePickerWidget*       addFilePicker(const QString&);
	Widget2D*				addVector2Widget(const QString&, float xValue, float yValue);
	Widget3D*				addVector3Widget(const QString&, float xValue, float yValue, float zValue);
	/// The COMPACT, SCRUBBABLE rows (ui/controls/dragvaluewidgets.h) — the
	/// transform editor's shape, for panels that want a labelled number rather
	/// than the unlabelled full-width spinboxes addVector3Widget produces (it
	/// ignores its name argument entirely). New panel rows should use these.
	DragFloatWidget*		addDragFloat(const QString &title, double value,
	                                     double min, double max,
	                                     double perPixelStep = 0.02, int decimals = 3);
	DragVector3Widget*		addDragVector3(const QString &title, const iris::Vec3 &value,
	                                       double min = -100000.0, double max = 100000.0,
	                                       double perPixelStep = 0.02, int decimals = 3);
	Widget4D*				addVector4Widget(const QString&, float xValue, float yValue, float zValue, float wValue);
	CubeMapWidget*			addCubeMapWidget(QStringList list);
	CubeMapWidget*			addCubeMapWidget();
	CubeMapWidget*			addCubeMapWidget(QString top, QString bottom, QString left, QString front, QString right, QString back);

    PropertyWidget*         addPropertyWidget();
    /// A NESTED, collapsible section inside this blade, titled and closed.
    /// Returned so the caller can fill it with the same add*() helpers.
    ///
    /// Exists so a panel can group a long tail of secondary rows (the
    /// material panel's Detail Layers, MATERIAL_GAPS_SPEC GAP 2) without
    /// widening addRow(): "the one place a row enters a blade" stays private
    /// and this is a named capability rather than a hole in it.
    AccordianBladeWidget*   addSection(const QString &title);

    /// Drops a caller-built widget into the blade's content pane, exactly where
    /// the add*() helpers put theirs. For rows the generic controls do not
    /// cover (the light panel's two asset-binding rows).
    void                    addWidgetToContent(QWidget *widget);

    /// The one live Project (Phase 4: was the Globals::project static). Set by
    /// whoever creates the panel; the add*() helpers above forward it to the
    /// controls they build, which read it in their drop handlers.
    Project *project = nullptr;
    virtual void setProject(Project *p) { project = p; }

    void setPanelTitle(const QString&);
    void collapse();
    void expand();

    /// Retires every row in the content pane. `layout` is IGNORED and has
    /// always been (the pane is the only thing this clears); it stays only
    /// because every call site passes `this->layout()`.
    void clearPanel(QLayout *layout);
    /// How many retired rows are still alive (see clearPanel's note on the
    /// generations). A diagnostic, and the thing ui.selection_cost asserts on.
    int retiredRowCount() const;
    /// THE HEADROOM, STATED EXACTLY. A retired row survives
    /// kRetiredGenerations top-level clearPanel() calls and is freed by the
    /// next one. The call that retires it is the FIRST of those, and that call
    /// is normally the rebuild the row itself asked for — so a row may drive
    /// `kRetiredGenerations - 1` FURTHER rebuilds from inside its own slot and
    /// still be readable when they return.
    ///
    /// Today's measured maximum from a row's own signal is ZERO further
    /// rebuilds (one rebuild in total, lead review F3), so three generations is
    /// two of slack. ui.selection_cost drives the promise at its boundary from
    /// a genuinely retired row, and one PAST it fails — verified by driving
    /// kRetiredGenerations from the slot, which loses the row.
    static constexpr int kRetiredGenerations = 3;
    int minimum_height, stretch;

    void stepHeight(int h) {
        this->minimum_height += h;
    }

    void resetHeight() {
        this->minimum_height = 0;
    }

    void setHeight(int h) {
        this->minimum_height = h;
    }

private slots:
    void onPanelToggled();

private:
    /// The one place a row enters a blade. Fits the row to the dock
    /// (ui/controls/rowfit.h) and then adds it to the content pane — every
    /// add*() helper above goes through here.
    void addRow(QWidget *row);
    /// clearPanel's actual loop, recursing into nested layouts properly.
    void drainLayout(QLayout *layout);

    Ui::AccordianBladeWidget *ui;
    /// Rows retired by the last kRetiredGenerations top-level clearPanel()
    /// calls, newest first — see that function for why a ring and not one list.
    /// QPointer because deleteLater() may have collected them first (an
    /// interactive session turns the event loop; a script run does not, which
    /// is the whole reason this exists).
    QList<QPointer<QWidget>> mRetired[kRetiredGenerations];
    /// Re-entrancy depth. The generation shift happens once per TOP-LEVEL call;
    /// a clear entered from inside another one (the child-layout recursion, or
    /// a rebuild driven from a row's slot mid-clear) must not consume a
    /// generation of its own.
    int mClearDepth = 0;
};

#endif // ACCORDIANBLADEWIDGET_H
