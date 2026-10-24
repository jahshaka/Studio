/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIGHTPROPERTYWIDGET_H
#define LIGHTPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "ui/panels/propertywidgets/panelundo.h"

class ColorValueWidget;
class ColorPickerWidget;
class Database;
class LightChannelsWidget;
class QLabel;
class QPushButton;
namespace iris
{
    class SceneNode;
    class LightNode;
}


/**
 * This class displays properties of light nodes
 */
class LightPropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    LightPropertyWidget(QWidget* parent=nullptr);

    /// The library, for the asset-binding row (the IES profile).
    /// Injected by SceneNodePropertiesWidget like the other panels'.
    void setDatabase(Database *database) { db = database; }

    /**
     * Sets the active sceneNode. If the sceneNode is a LightNode it is casted
     * to a LightNode and stored in lightNode, otherwise lightNode is reset to null
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

    /// The undo stack (debt L6). Every row on this blade writes a REFLECTED
    /// light property, so each becomes one SetNodePropertyCommand — the same
    /// command node.setProperty pushes. Nullable.
    void setServices(StudioServices *s) { services = s; }

protected slots:

    /// Binds/clears the IES photometric profile through LightBindings — the
    /// SAME implementation the node.setLightProfile verb uses.
    void pickProfile();
    void clearProfile();

    /// Lighting channels, light side: which channels this light illuminates.
    void lightChannelsChanged(quint32 mask);

private:
    /// Binds every row to its reflected property. Called once, from the ctor.
    void wireRows();
    /// Binds (or clears) the IES profile through LightBindings and records ONE
    /// undo step. The pick and clear buttons are two front ends on it.
    void bindProfile(const QString &guid);
    /// Repaints the binding row from the node, INCLUDING the honesty
    /// annotation: a profile does nothing on a shadow-casting point light (the
    /// renderer has no profile term there). It is silent in the renderer, so
    /// the panel has to say it out loud.
    void refreshBindingRows();

    QString evalShadowTypeName(iris::ShadowMapType shadowType);
    iris::ShadowMapType evalShadowMapType(QString shadowType);

private:

    QSharedPointer<iris::LightNode> lightNode;
    StudioServices *services = nullptr;
    /// Populating the rows for a newly selected light is not a user edit (the
    /// sliders emit from setValue) — see rowundo::Binding::guard.
    bool loading = false;
    panelundo::NodeRows rows;

	ColorValueWidget* lightColor;
	HFloatSliderWidget* distance;
	HFloatSliderWidget* sourceRadius;
    HFloatSliderWidget* spotCutOff;
    HFloatSliderWidget* spotCutOffSoftness;
    HFloatSliderWidget* spotFalloff;
    HFloatSliderWidget* intensity;
    // Area lights (engine viewport): rectangle size, emission sides.
    HFloatSliderWidget* rectWidth;
    HFloatSliderWidget* rectHeight;
    CheckBoxWidget* doubleSided;
    /// Lighting channels (light masks).
    LightChannelsWidget* lightChannels = nullptr;
    //EnumPicker* lightTypePicker;

	// (`shadowColor`, `shadowAlpha` and their `mShadowTintSupported` gate are
	// GONE with the document fields — render audit I-6. There is no per-light
	// shadow tint on this engine and there never was on the last one either.)
	bool mPointShadowsSupported = false;   // engine mode: point lights get Shadow Type/Size

    Database *db = nullptr;
    QWidget *profileRow = nullptr;
    QLabel *profileLabel = nullptr;
    QPushButton *profilePick = nullptr;
    QPushButton *profileClear = nullptr;
    QLabel *profileNote = nullptr;

    /// THE SUN, directional lights only (SUN_AND_LIGHT_DEFAULTS Q1). The
    /// readout says which light IS the sun and why; the number is the row that
    /// decides it. Both hidden on every other light type.
    class LabelWidget*      sunReadout = nullptr;
    class DragFloatWidget*  forwardShadingPriority = nullptr;
    /// FOLLOWS ATMOSPHERE — the sun's row, shown beside Sun Angle and only on
    /// the resolver's sun (lightnode.h says what it does).
    CheckBoxWidget *followsAtmosphere = nullptr;
    /// Re-reads the scene's sun and re-labels the two rows above.
    void refreshSunRows();

    ComboBoxWidget* shadowType;
    ComboBoxWidget* shadowSize;
};

#endif // LIGHTPROPERTYWIDGET_H
