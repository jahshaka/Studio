/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPROPERTYWIDGET_H
#define MATERIALPROPERTYWIDGET_H

#include <QWidget>

#include "io/assetmanager.h"
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/document/materials/material.h"
#include "irisgl/core/properties/property.h"

namespace iris {
    class SceneNode;
    class MeshNode;
    class Material;
}

class PropertyWidget;
class ComboBoxWidget;
class Database;
struct StudioServices;

/**
 *  Displays properties for materials
 */
#include <QPointer>
#include <QPushButton>


class MaterialPropertyWidget : public AccordianBladeWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    MaterialPropertyWidget() = default;

    // ONE material, ONE path (HLMS_ADOPTION P4b). This widget used to carry a
    // CustomMaterial member AND a generic one, with a dynamicCast choosing
    // between them and every method branching on which was set — a split that
    // existed only because a shader-graph material was a different class. It is
    // not any more: every material a mesh can carry is rendered from
    // Material::properties.
    iris::MaterialPtr material;

    /// Kept as the name the rest of the widget reads through.
    iris::MaterialPtr currentMaterial() const { return material; }

    void setSceneNode(iris::SceneNodePtr sceneNode);
    /// "Reset to <provider>": present only while the shown node has a default
    /// material of its own (services/materialdefaults.h); null otherwise.
    QPushButton *resetMaterialButton() const { return resetButton.data(); }
    void setWidgetProperties();

    void setServices(StudioServices *s) { services = s; }
    void setDatabase(Database *db) {
        this->db = db;
    }


    /// The texture paths the CURRENT material had when this panel adopted it —
    /// the "before" side of updateTextureDependency's project bookkeeping.
    /// Public so the blade-reuse contract is assertable: the properties panel
    /// keeps its blades as hidden children and reuses them across selections,
    /// so this must be a snapshot of ONE material and never an accumulation.
    const QMap<QString, QString> &shownTextures() const { return existingTextures; }

    /// How many mesh picks this blade answered by REFILLING the rows it already
    /// had, and how many needed a full rebuild (ADD-1). Reported by
    /// `editor.propertiesStats()`; never used to make a decision.
    int refillCount() const { return refills; }
    int rebuildCount() const { return rebuilds; }

    /// THE MATERIAL COMBO — the presets and library materials this blade offers,
    /// and which of them is selected. Public so COMBO-FP-1's contract is
    /// assertable: what the combo holds must follow the LIBRARY, not just its
    /// item count (ui.material_panel).
    ComboBoxWidget *materialCombo() const { return materialSelector; }

protected slots:
    void materialChanged(int);

private:
    void addResetRow();
    /// Drops every row this blade is showing (and forgets the widgets that went
    /// with them). The ONE place that clears this panel.
    void clearShownRows();
    /// Points the rows already on screen at another mesh's material, when they
    /// can show it (ADD-1). False means "rebuild" and is always safe.
    bool rebindTo(const QSharedPointer<iris::MeshNode> &node, const iris::MaterialPtr &mat);
    /// The base / "detail*" split of a material's property list, by the
    /// document's own naming rule.
    static void splitRows(const iris::MaterialPtr &mat,
                          QList<iris::Property *> &base,
                          QList<iris::Property *> &details);
    /// THE MATERIAL COMBO'S ENTRIES, AS A STRING (COMBO-FP-1). `materialItemsKey`
    /// is the (guid, label) list setupShaderSelector would build RIGHT NOW;
    /// `comboItemsKey` is the list the combo on screen actually holds. Equal
    /// means the combo is current and a refill may keep it.
    QString materialItemsKey() const;
    QString comboItemsKey() const;
    /// Guarded: clearPanel() deletes the row with every other row.
    QPointer<QPushButton> resetButton;
    QSharedPointer<iris::MeshNode> meshNode;
    /// Both are destroyed by clearShownRows and rebuilt by the full path; null
    /// between the two (they were uninitialised members reading as garbage on
    /// the first pick of a session — nothing dereferenced them before the
    /// refill path did).
    ComboBoxWidget* materialSelector = nullptr;
    PropertyWidget* materialPropWidget = nullptr;
    /// The rows of the collapsible "Detail Layers" section (GAP 2). Held so the
    /// listener can tell which widget a change came from; null when the
    /// material declares no detail rows.
    PropertyWidget* detailPropWidget = nullptr;

    void setupShaderSelector();
    /// Re-reads `existingTextures` from the material currently shown (empty when
    /// there is none). Called wherever the shown material changes.
    void snapshotTextures();
    void updateTextureDependency(iris::Property*);
    void onPropertyChanged(iris::Property*) override;
    void onPropertyChangeStart(iris::Property*) override;
    void onPropertyChangeEnd(iris::Property*) override;

    // for undo/redo
    QVariant startValue;
    /// True between onPropertyChangeStart and onPropertyChangeEnd — i.e. only
    /// for a gesture this panel actually saw begin. A gesture that spans the
    /// end of a script run (the rows are inert while one is in flight) must
    /// not record a step against a stale startValue.
    bool gestureOpen = false;
    Database *db = nullptr;
    StudioServices *services = nullptr;
    QString meshNodeGuid;
    int refills = 0;
    int rebuilds = 0;
    QMap<QString, QString> existingTextures;
};

#endif // MATERIALPROPERTYWIDGET_H
