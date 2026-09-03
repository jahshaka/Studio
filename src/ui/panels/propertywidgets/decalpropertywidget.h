/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DECALPROPERTYWIDGET_H
#define DECALPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "irisgl/irisglfwd.h"
#include "ui/controls/accordionbladewidget.h"

namespace iris {
    class SceneNode;
    class DecalNode;
}

class Database;
class Project;
class StudioServices;
class TexturePickerWidget;
class LabelWidget;

/**
 * Decal properties (DECALS_SPEC §5.6): the projected image, the projector box
 * and the two surface values a decal overwrites.
 *
 * There is deliberately NO opacity and NO colour tint here. The renderer packs
 * exactly four floats per decal (three rows of the inverse world matrix plus
 * indices/metalness/roughness); either control would mean owning a fork of the
 * PBS shader template, which this project does not do.
 */
class DecalPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    DecalPropertyWidget(QWidget *parent = nullptr);

    void setDatabase(Database *db) { this->db = db; }
    void setProject(Project *project);
    void setServices(StudioServices *services) { this->services = services; }

    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

protected slots:
    void onImageChanged(const QString &path, const QString &guid);
    void onNormalChanged(const QString &path, const QString &guid);
    void onEmissiveChanged(const QString &path, const QString &guid);
    void onWidthChanged(float v);
    void onHeightChanged(float v);
    void onDepthChanged(float v);
    void onMetalnessChanged(float v);
    void onRoughnessChanged(float v);
    void onIgnoreAlphaChanged(bool v);

private:
    /// Re-resolves a guid to CAS bytes and pins it as a project BINDING
    /// (ProjectAssets::AddKind::Binding — never a direct add, so no companion
    /// material is minted for a decal image). Shared by the three pickers.
    void bindMap(const QString &guid, QString &guidField, QString &pathField);

    QSharedPointer<iris::DecalNode> decalNode;
    Database *db = nullptr;
    Project *project = nullptr;
    StudioServices *services = nullptr;
    bool loading = false;

    TexturePickerWidget *image = nullptr;
    TexturePickerWidget *normalImage = nullptr;
    TexturePickerWidget *emissiveImage = nullptr;
    HFloatSliderWidget *width = nullptr;
    HFloatSliderWidget *height = nullptr;
    HFloatSliderWidget *depth = nullptr;
    HFloatSliderWidget *metalness = nullptr;
    HFloatSliderWidget *roughness = nullptr;
    CheckBoxWidget *ignoreAlpha = nullptr;
};

#endif // DECALPROPERTYWIDGET_H
