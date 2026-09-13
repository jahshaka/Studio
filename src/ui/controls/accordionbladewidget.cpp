/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <utility>
#include "ui/controls/accordionbladewidget.h"
#include "ui_accordionbladewidget.h"

#include "ui/controls/hfloatsliderwidget.h"
#include "ui_hfloatsliderwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui_colorvaluewidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui_texturepickerwidget.h"
#include "ui/panels/transformeditor.h"
#include "ui/controls/checkboxwidget.h"
#include "ui_checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui_comboboxwidget.h"
#include "ui/controls/textinputwidget.h"
#include "ui_textinputwidget.h"
#include "ui_labelwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui_labelwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/filepickerwidget.h"
#include "ui_filepickerwidget.h"
#include "ui/panels/propertywidget.h"
#include "ui_propertywidget.h"

#include "ui/panels/propertywidgets/cubemapwidget.h"
#include "ui/controls/rowfit.h"
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"


// TODO - omit height calculation
AccordianBladeWidget::AccordianBladeWidget(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::AccordianBladeWidget)
{
    ui->setupUi(this);
    // accordionbladewidget.ui used to embed these (classic-only now; theme sweep)
    setStyleSheet(StyleSheet::AccordionBladeRoot());
    // Qlementine: the section header is a band in the theme's neutral colour
    // with a flat chevron — the style's own button and label on a palette.
    ThemeRoles::setSurface(ui->bg, ThemeRoles::Surface::Band);
    ThemeRoles::setFlat(ui->toggle);

    stretch = 0;
    setMinimumHeight(ui->bg->height());
    minimum_height = minimumHeight();

    connect(ui->toggle, SIGNAL(toggled(bool)), SLOT(onPanelToggled()));

    ui->toggle->setIconSize(QSize(24, 24));

    // The section NAME is a row too — "Rayon — Realtime Global Illumination" is
    // 250 px of text, and a QLabel's minimum width is the whole of it. Elide it
    // like every other label instead of letting the title decide how wide the
    // dock has to be.
    RowFit::fitLabel(ui->content_title);

	collapse();
}

AccordianBladeWidget::~AccordianBladeWidget()
{
    delete ui;
}

/// EVERY ROW A BLADE SHOWS GOES THROUGH HERE, which is what makes "the panel
/// fits its dock" a property of the accordion rather than of seventeen panels
/// (owner report 2026-09-08, ui.properties_width). RowFit gives the row the
/// size behaviour a docked property row needs: names elide, controls shrink,
/// nothing forces the panel wider than the column it lives in.
void AccordianBladeWidget::addRow(QWidget *row)
{
    if (!row) return;
    RowFit::fitRow(row);
    ui->contentpane->layout()->addWidget(row);
}

void AccordianBladeWidget::clearPanel(QLayout *layout)
{
    Q_UNUSED(layout);   // see drainLayout: this always clears the CONTENT PANE
    if (ui->contentpane->layout() == nullptr) return;

    // THE GENERATIONS BEFORE LAST GO NOW (MIRROR_SCALE lane, 2026-09-13).
    //
    // These rows used to be retired with deleteLater() alone, and that is a
    // trap in any run that rebuilds a blade many times without returning to the
    // event loop — which is EVERY script- or MCP-driven scene build, since a
    // script run is one call that never yields. Nothing collects a
    // DeferredDelete until the loop turns, so both the blade's child list and
    // Qt's GLOBAL posted-event list grow by a row per row per rebuild, and Qt
    // scans that list on every widget construction (QApplicationPrivate::
    // compressEvent) and every widget destruction (QCoreApplication::
    // removePostedEvents). The result is quadratic: measured on this tree,
    // `scene.addPrimitive` in a loop cost 68 ms per cube at 35 nodes and 311 ms
    // at 155 — each add rebuilds the material blade, and each rebuild made the
    // next one slower. The app then spent MINUTES in ~MainWindow destroying the
    // thousands of orphaned rows, each one scanning the same list again.
    //
    // WHY A RING OF GENERATIONS AND NOT ONE LIST. A rebuild is very often
    // triggered BY one of the rows being retired (a combo box's activation
    // handler asks the panel to rebuild), so the generation this call is
    // retiring can be sitting on the stack and must not be freed here — that is
    // what deleteLater is for and it stays. The generations BEFORE it can only
    // still be on the stack if one row's slot drove SEVERAL rebuilds in a row,
    // and this file cannot know how many a caller will drive.
    //
    // So the headroom is a NUMBER and it is stated (see kRetiredGenerations):
    // a retired row survives kRetiredGenerations of these calls and is freed by
    // the next, and since the call that retired it is normally the rebuild the
    // row itself asked for, a row may drive kRetiredGenerations - 1 FURTHER
    // rebuilds from its own slot. Today's measured maximum is zero further
    // (one rebuild in total, lead review F3), so three generations is two of
    // slack — and ui.selection_cost drives the boundary from a genuinely
    // retired row, under ASan, so a generation too few is a red gate rather
    // than a mystery crash in somebody's session.
    //
    // AND NOT WHILE A CLEAR IS ON THE STACK. The shift happens once per
    // TOP-LEVEL call: the nested case (a rebuild entered from inside this very
    // function, or the recursion into a child layout) would otherwise consume a
    // generation per nesting level, which is the one shape a fixed headroom
    // cannot absorb. mClearDepth makes it impossible instead of unlikely.
    if (mClearDepth == 0) {
        for (const QPointer<QWidget> &w : std::as_const(mRetired[kRetiredGenerations - 1]))
            if (w) delete w.data();
        for (int g = kRetiredGenerations - 1; g > 0; --g)
            mRetired[g] = mRetired[g - 1];
        mRetired[0].clear();
    }

    ++mClearDepth;
    drainLayout(ui->contentpane->layout());
    --mClearDepth;
}

/// The drain itself, which HONOURS ITS ARGUMENT.
///
/// clearPanel's recursion used to call itself with the child layout and then
/// ignore it (the parameter has always been dead), so a nested layout's rows
/// were never hidden, never retired and never freed — they simply stayed as
/// children of the blade. This is the same loop, recursing properly.
void AccordianBladeWidget::drainLayout(QLayout *layout)
{
    if (!layout) return;
    while (auto item = layout->takeAt(0)) {
        if (auto widget = item->widget()) {
            // HIDE, THEN retire. deleteLater() defers the destruction to the
            // next event-loop turn, and a widget that has left the layout is
            // still a VISIBLE child sitting at its old geometry — so between an
            // edit that rebuilds a blade and that turn, the retired rows paint
            // ON TOP OF the new ones. That is the "garbled Properties rows"
            // shape (debt A5b): it needs a repaint inside the window, which a
            // page switch or a dock resize provides. SceneNodePropertiesWidget::
            // clearLayout has hidden its blades for the same reason since the
            // selection-cost fix; this is the row-level twin of it.
            widget->hide();
            widget->deleteLater();
            mRetired[0].append(widget);
        }

        if (auto childLayout = item->layout()) drainLayout(childLayout);

        delete item;
    }
}

/// How many retired rows this blade is still holding. kRetiredGenerations at
/// most (see clearPanel); ui.selection_cost asserts it does not grow with the
/// number of rebuilds, which is the shape the quadratic defect had.
int AccordianBladeWidget::retiredRowCount() const
{
    int n = 0;
    for (int g = 0; g < kRetiredGenerations; ++g)
        for (const QPointer<QWidget> &w : mRetired[g]) if (w) ++n;
    return n;
}

void AccordianBladeWidget::onPanelToggled()
{
    if (ui->contentpane->isVisible()) {
		collapse();
    } else {
        expand();
    }
}

void AccordianBladeWidget::setPanelTitle(const QString& title)
{
    ui->content_title->setText(title);
}

TransformEditor* AccordianBladeWidget::addTransformControls()
{
    auto transformEditor = new TransformEditor();

    int height = transformEditor->height();
    int spacing = ui->contentpane->layout()->spacing();

    minimum_height += height;

    addRow(transformEditor);
    ui->contentpane->layout()->setContentsMargins(0, 0, 0,0);

    return transformEditor;
}

ColorValueWidget* AccordianBladeWidget::addColorPicker(const QString& name)
{
    auto colorpicker = new ColorValueWidget();
    colorpicker->setLabel(name);

    minimum_height += colorpicker->height() + stretch;

    addRow(colorpicker);
    return colorpicker;
}

TexturePickerWidget* AccordianBladeWidget::addTexturePicker(const QString& name)
{
    auto texpicker = new TexturePickerWidget();
    texpicker->project = project;
    texpicker->ui->label->setText(name);

    minimum_height += texpicker->height() + stretch;

    addRow(texpicker);
    return texpicker;
}

FilePickerWidget* AccordianBladeWidget::addFilePicker(const QString &name)
{
    FilePickerWidget *filePicker = new FilePickerWidget();
    filePicker->ui->label->setText(name);
//    filePicker->suffix = suffix;

    minimum_height += filePicker->height() + stretch;

    addRow(filePicker);
    return filePicker;
}

Widget2D * AccordianBladeWidget::addVector2Widget(const QString &, float xValue, float yValue)
{
	auto widget = new Widget2D;
	widget->setValues(xValue, yValue);
	minimum_height += widget->height() + stretch;
	addRow(widget);
	return widget;
}

Widget3D * AccordianBladeWidget::addVector3Widget(const QString &, float xValue, float yValue, float zValue)
{
	auto widget = new Widget3D;
	widget->setValues(xValue, yValue, zValue);
	minimum_height += widget->height() + stretch;
	addRow(widget);
	return widget;
}

DragFloatWidget *AccordianBladeWidget::addDragFloat(const QString &title, double value,
                                                   double min, double max,
                                                   double perPixelStep, int decimals)
{
	auto widget = new DragFloatWidget(title);
	widget->setDecimals(decimals);
	widget->setRange(min, max);
	widget->setPerPixelStep(perPixelStep);
	widget->setValue(value);
	minimum_height += widget->height() + stretch;
	addRow(widget);
	return widget;
}

DragVector3Widget *AccordianBladeWidget::addDragVector3(const QString &title, const iris::Vec3 &value,
                                                        double min, double max,
                                                        double perPixelStep, int decimals)
{
	auto widget = new DragVector3Widget(title);
	widget->setDecimals(decimals);
	widget->setRange(min, max);
	widget->setPerPixelStep(perPixelStep);
	widget->setValues(value);
	minimum_height += widget->height() + stretch;
	addRow(widget);
	return widget;
}

Widget4D * AccordianBladeWidget::addVector4Widget(const QString &, float xValue, float yValue, float zValue, float wValue)
{
	auto widget = new Widget4D;
	widget->setValues(xValue, yValue, zValue, wValue);
	minimum_height += widget->height() + stretch;
	addRow(widget);
	return widget;
}

CubeMapWidget* AccordianBladeWidget::addCubeMapWidget(QStringList list)
{
	auto widget = new CubeMapWidget();
	widget->project = project;
	widget->addCubeMapImages(list);
	minimum_height += widget->height() + stretch;
	addRow(widget);
	return widget;
}

CubeMapWidget* AccordianBladeWidget::addCubeMapWidget()
{
	auto widget = new CubeMapWidget(this);
	widget->project = project;
	minimum_height += widget->height() + stretch;
	addRow(widget);
	return widget;
}

CubeMapWidget* AccordianBladeWidget::addCubeMapWidget(QString top, QString bottom, QString left, QString front, QString right, QString back)
{
	return addCubeMapWidget({top,bottom,left,front,right,back});
}

PropertyWidget *AccordianBladeWidget::addPropertyWidget()
{
    PropertyWidget *props = new PropertyWidget;
    props->project = project;
    addRow(props);
    return props;
}

AccordianBladeWidget *AccordianBladeWidget::addSection(const QString &title)
{
    auto *section = new AccordianBladeWidget(this);
    section->setPanelTitle(title);
    section->project = project;   // the pickers inside it need the live Project
    addRow(section);              // starts COLLAPSED — the ctor collapses
    return section;
}

HFloatSliderWidget* AccordianBladeWidget::addFloatValueSlider(
        const QString& name,
        float start,
        float end,
        float value)
{
    auto slider = new HFloatSliderWidget();
    slider->ui->label->setText(name);
    slider->setRange(start, end);
    slider->setValue(value);

    minimum_height += slider->height() + stretch;

    addRow(slider);
    return slider;
}

CheckBoxWidget* AccordianBladeWidget::addCheckBox(const QString& title, bool value)
{
    auto checkbox = new CheckBoxWidget();
    checkbox->setLabel(title);

    minimum_height += checkbox->height() + stretch;

    addRow(checkbox);
    return checkbox;
}

void AccordianBladeWidget::addWidgetToContent(QWidget *widget)
{
    if (!widget) return;
    minimum_height += widget->sizeHint().height() + stretch;
    addRow(widget);
}

ComboBoxWidget* AccordianBladeWidget::addComboBox(const QString& title)
{
    auto combobox = new ComboBoxWidget();
    combobox->setLabel(title);

    minimum_height += combobox->height() + stretch;

    addRow(combobox);
    return combobox;
}

TextInputWidget* AccordianBladeWidget::addTextInput(const QString& title)
{
    auto textInput = new TextInputWidget();
    textInput->setLabel(title);

    minimum_height += textInput->height() + stretch;

    addRow(textInput);
    return textInput;
}

LabelWidget* AccordianBladeWidget::addLabel(const QString& title, const QString& text)
{
    auto label = new LabelWidget();
    label->setLabel(title);
    label->setText(text);

    minimum_height += label->height() + stretch;

    addRow(label);
    return label;
}

void AccordianBladeWidget::collapse()
{
	ui->toggle->setIcon(QIcon(":/icons/right-chevron.svg"));
	ui->contentpane->setVisible(false);
	this->setMinimumHeight(ui->bg->height());
}

void AccordianBladeWidget::expand()
{
    // this is a tad bit hacky and there is definitely a better way to do this automatically
    // for now, we calculate and set the accordion height including spacing and margins
    // int widgetCount = ui->contentpane->layout()->count();
    // int topMargin, bottomMargin;
    // int spacing = ui->contentpane->layout()->spacing();
    // ui->contentpane->layout()->getContentsMargins(nullptr, &topMargin, nullptr, &bottomMargin);
    // int finalHeight = minimum_height + (widgetCount * spacing) + topMargin + bottomMargin;

    this->setMinimumHeight(0);
    // this->setMaximumHeight(finalHeight);
	ui->toggle->setIcon(QIcon(":/icons/chevron-arrow-down.svg"));
    ui->contentpane->setVisible(true);
}

