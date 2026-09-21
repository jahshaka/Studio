/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROPERTYWIDGET_H
#define PROPERTYWIDGET_H

#include <QWidget>
#include "irisgl/core/properties/property.h"
#include "modules/materials/propertywidgets/propertywidgetbase.h"

namespace Ui {
    class PropertyWidget;
}

class HFloatSliderWidget;
class ColorValueWidget;
class CheckBoxWidget;
class TexturePickerWidget;
class FilePickerWidget;

class BaseWidget;

class Project;

class PropertyWidget : public QWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    explicit PropertyWidget(QWidget *parent = 0);
    ~PropertyWidget();

    /// The one live Project (Phase 4: was the Globals::project static). Set by
    /// AccordianBladeWidget::addPropertyWidget(); forwarded to the texture
    /// pickers this widget builds.
    Project *project = nullptr;

    void addProperty(const iris::Property*);
    void setProperties(QList<iris::Property*>);

    /// CAN THESE ROWS SHOW THAT LIST? (ADD-1) True when `props` has the same
    /// SHAPE as the list on screen — the same order, types, names, labels,
    /// ranges and enum vocabularies — i.e. when the rows this panel already
    /// holds would have been built identically for it.
    bool canRebind(const QList<iris::Property *> &props) const;
    /// Points the EXISTING rows at another property list: nothing destroyed,
    /// nothing created, no popup rebuilt, no row retired from the property-row
    /// registry. Only legal when canRebind() says so.
    void rebind(const QList<iris::Property *> &props);

    /// THE DOCUMENT POINTERS GO (lane OPEN-FRAMES-1, from the §497 witness).
    ///
    /// `properties` is a bare list of `iris::Property *` INTO the document's
    /// material — a panel's window onto objects it does not own and is not
    /// told about when they die. Every handler these rows install reads it
    /// (propertyAt) and canRebind DEREFERENCES the stored side, comparing
    /// names, labels and enum vocabularies. A dangling entry is therefore both
    /// a read and, through a row's value-changed handler, a WRITE into freed
    /// memory: one Qt assert of this shape (`str || !len` in QStringView, out
    /// of canRebind) was caught with a witness on 2026-09-15.
    ///
    /// This drops them. Afterwards propertyAt() answers null for every slot —
    /// so the rows are inert rather than dangerous — and canRebind() answers
    /// false, so the panel rebuilds, which is always correct.
    ///
    /// Called automatically when the blade RETIRES this widget (see the
    /// QEvent::DynamicPropertyChange handler in event(): a retired row reads
    /// null through RowPtr, and this is the same rule applied to the document
    /// pointers the row is holding). Callable by hand by anything that knows
    /// the list is about to die.
    void forgetProperties();
    QList<iris::Property*> getProperties() { return properties; }

protected:
    /// Watches for the blade's retirement mark (bladerow::markRetired sets a
    /// dynamic property, synchronously) and drops the document pointers then.
    bool event(QEvent *e) override;

public:
    int getHeight();

    HFloatSliderWidget  *addFloatValueSlider(const QString&, float min, float max);
    ColorValueWidget    *addColorPicker(const QString&);
    CheckBoxWidget      *addCheckBox(const QString&);
    TexturePickerWidget *addTexturePicker(const QString&);
    FilePickerWidget    *addFilePicker(const QString &name, const QString &suffix);
	Widget2D*			addVector2Widget(const QString&, float xValue, float yValue);
	Widget3D*			addVector3Widget(const QString&, float xValue, float yValue, float zValue);
	Widget4D*			addVector4Widget(const QString&, float xValue, float yValue, float zValue, float wValue);
	QWidget*			addWidgetHolder(QString title, QWidget *widget);

    void addFloatProperty(iris::Property*);
    void addIntProperty(iris::Property*);
    /// The generic ENUM row: a labeled dropdown built from ListProperty's own
    /// `labels`, combo index == stored value. Every enum row in the app goes
    /// through here — there is deliberately no second by-name special case.
    void addEnumProperty(iris::Property*);
    void addColorProperty(iris::Property*);
    void addBoolProperty(iris::Property*);
    void addTextureProperty(iris::Property*);
    void addFileProperty(iris::Property*);

	void addVector2Property(iris::Property*);
	void addVector3Property(iris::Property*);
	void addVector4Property(iris::Property*);

    void setListener(iris::PropertyListener*);

signals:
    void onPropertyChanged(iris::Property*);
    void onPropertyChangeStart(iris::Property*);
    void onPropertyChangeEnd(iris::Property*);

private:
    QList<iris::Property*> properties;
    /// The CONTROL each property built, in slot order (the row added to the
    /// layout may be a holder around it — the vector rows). Aligned with
    /// `properties`; this is what rebind() writes into.
    QList<QWidget*> valueRows;
    /// True while rebind() is putting values into the rows: the handlers are
    /// inert, so filling a slider is not an edit of the material it reads from.
    bool rebinding = false;
    iris::PropertyListener *listener = nullptr;
    int progressiveHeight, stretch;

    /// Records a property's row and returns its slot (see propertyAt).
    int takeSlot(iris::Property *prop, QWidget *valueRow);
    /// The property a row is bound to RIGHT NOW — null while rebinding.
    iris::Property *propertyAt(int slot) const;

    /// The row widget each property built, by property name. Only needed by
    /// rows that CONSTRAIN other rows (see applyRowConstraints).
    QHash<QString, QWidget*> rowByName;

    /// Cross-row availability. One rule today: the two clear-coat rows are
    /// disabled unless the material's BRDF is in the Default family, because
    /// the renderer cannot carry a coat on any other one. Disabling (rather
    /// than zeroing) is the decided behaviour — the authored coat survives a
    /// round trip through another BRDF (HLMS_ADOPTION_SPEC D-P1b).
    void applyRowConstraints();

    void updatePane();

    /// The one place a property row enters this panel; fits it to the dock
    /// (ui/controls/rowfit.h) on the way in, and hands it to the property-row
    /// registry with the key below.
    void addRow(QWidget *row);
    /// The property whose row is being built right now — its `name` becomes the
    /// row's filter key (PROPERTY_FILTER_SPEC §3.2). Empty outside
    /// setProperties().
    QString pendingKey;

    Ui::PropertyWidget *ui;
};

#endif // PROPERTYWIDGET_H
