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

    /// The library, for the two asset-binding rows (IES profile, area mask).
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

    /// The one row with a consequence beyond its own value: an accurate (LTC)
    /// area light ignores its mask, and the panel says so.
    void lightAccurateChanged(bool accurate);

    /// Binds/clears the IES photometric profile and the area-light mask. Both
    /// go through LightBindings — the SAME implementation the
    /// node.setLightProfile / node.setLightTexture verbs use.
    void pickProfile();
    void clearProfile();
    void pickMask();
    void clearMask();

    /// Lighting channels, light side: which channels this light illuminates.
    void lightChannelsChanged(quint32 mask);

private:
    /// Binds every row to its reflected property. Called once, from the ctor.
    void wireRows();
    /// Binds (or clears) the IES profile / the area mask through LightBindings
    /// and records ONE undo step. The pick and clear buttons are two front ends
    /// on each of these.
    void bindProfile(const QString &guid);
    void bindMask(const QString &guid);
    /// Repaints the two binding rows from the node, INCLUDING the honesty
    /// annotations: a profile does nothing on a shadow-casting point light (the
    /// renderer has no profile term there) and a mask does nothing on an
    /// accurate/LTC area light. Both are silent in the renderer, so the panel
    /// has to say them out loud.
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
    HFloatSliderWidget* spotCutOff;
    HFloatSliderWidget* spotCutOffSoftness;
    HFloatSliderWidget* spotFalloff;
    HFloatSliderWidget* intensity;
    // Area lights (engine viewport): rectangle size, emission sides, LTC toggle.
    HFloatSliderWidget* rectWidth;
    HFloatSliderWidget* rectHeight;
    CheckBoxWidget* doubleSided;
    CheckBoxWidget* accurate;
    /// Lighting channels (light masks) — NOT the "Light Mask" rows above, which
    /// bind an area light's gobo image.
    LightChannelsWidget* lightChannels = nullptr;
    //EnumPicker* lightTypePicker;

	ColorValueWidget* shadowColor;
	HFloatSliderWidget* shadowAlpha;
	// False in engine mode: no per-light shadow tint exists there (controls hidden).
	bool mShadowTintSupported = true;
	bool mPointShadowsSupported = false;   // engine mode: point lights get Shadow Type/Size

    Database *db = nullptr;
    QWidget *profileRow = nullptr;
    QLabel *profileLabel = nullptr;
    QPushButton *profilePick = nullptr;
    QPushButton *profileClear = nullptr;
    QLabel *profileNote = nullptr;
    QWidget *maskRow = nullptr;
    QLabel *maskLabel = nullptr;
    QPushButton *maskPick = nullptr;
    QPushButton *maskClear = nullptr;
    QLabel *maskNote = nullptr;

    CheckBoxWidget* shadowStatic;
    ComboBoxWidget* shadowType;
    ComboBoxWidget* shadowSize;
    HFloatSliderWidget* shadowBias;
};

#endif // LIGHTPROPERTYWIDGET_H
