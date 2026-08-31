/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef NODEPROPERTIESPANEL_H
#define NODEPROPERTIESPANEL_H

// §3a selection-driven properties panel (MATERIALS_EVALUATOR_SPEC §3,
// MATERIALS_NODES_AUDIT §6.3 phase 2). Replaces the graph-global
// PropertyListWidget in the Effects page's right dock:
//   - a node selected  -> that node's editable values, through the same
//     per-type editor components the inline node widgets use. Every write
//     goes deserializeWidgetValue -> valueChanged -> graphInvalidated —
//     the exact graph.setValue path; no second edit stack.
//   - the master selected -> the material settings view (name, blend, cull,
//     bake resolution).
//   - nothing selected -> the graph settings view (the full settings set).

#include <QWidget>
#include <QJsonValue>

class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QFormLayout;
class ColorPickerWidget;
class GraphNodeScene;
class NodeGraph;
class NodeModel;
struct MaterialSettings;

class NodePropertiesPanel : public QWidget
{
	Q_OBJECT
public:
	explicit NodePropertiesPanel(QWidget* parent = nullptr);

	// Follows the scene's nodeSelected/nodeValueChanged signals. The panel
	// never owns the scene; call again whenever the page swaps scenes.
	void setScene(GraphNodeScene* scene);
	void setGraph(NodeGraph* graph);

	// null -> graph settings view; master -> material settings view;
	// any other node -> its editors.
	void showNode(NodeModel* node);
	NodeModel* currentNode() const { return mNode; }

	// re-read the current node's serialized value into the editors
	void refreshFromNode();
	// re-read graph->settings into the settings forms
	void refreshSettings();

	// which view is up — for tests and for the page
	enum class View { GraphSettings, MaterialSettings, Node };
	View currentView() const;

signals:
	// Master/graph settings edits. The page routes this into the same
	// MaterialSettingsChangeCommand the left settings dock uses.
	void settingsEdited(MaterialSettings settings);

private:
	void buildUi();
	QWidget* buildSettingsPage(bool compact);
	void rebuildNodeEditors();
	void writeValue(const QJsonValue& value);
	void emitSettings();
	void pickTextureForNode();

	GraphNodeScene* mScene = nullptr;
	NodeGraph* mGraph = nullptr;
	NodeModel* mNode = nullptr;
	bool mUpdating = false;

	QStackedWidget* mStack = nullptr;

	// settings forms — one full (graph view), one compact (master view)
	struct SettingsForm {
		QLineEdit* name = nullptr;
		QComboBox* blend = nullptr;
		QComboBox* cull = nullptr;
		QSpinBox* bakeResolution = nullptr;
		// full form only
		QComboBox* renderLayer = nullptr;
		QCheckBox* zwrite = nullptr;
		QCheckBox* depthTest = nullptr;
		QCheckBox* fog = nullptr;
		QCheckBox* castShadow = nullptr;
		QCheckBox* receiveShadow = nullptr;
		QCheckBox* acceptLighting = nullptr;
	};
	SettingsForm mGraphForm;    // stack page 0
	SettingsForm mMasterForm;   // stack page 1

	// node page (stack page 2)
	QWidget* mNodePage = nullptr;
	QLabel* mNodeTitle = nullptr;
	QLabel* mNodeType = nullptr;
	QWidget* mEditorHost = nullptr;
	QVBoxLayout* mEditorLayout = nullptr;

	// live editors for the current node (type-dependent)
	QVector<QDoubleSpinBox*> mNumberBoxes;   // float / vector2/3/4 components
	ColorPickerWidget* mColorSwatch = nullptr;
	QPushButton* mTextureButton = nullptr;
};

#endif // NODEPROPERTIESPANEL_H
