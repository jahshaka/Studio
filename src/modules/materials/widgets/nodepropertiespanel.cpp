/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "nodepropertiespanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "../graph/graphnodescene.h"
#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../nodes/test.h"
#include "../core/texturemanager.h"

#if (EFFECT_BUILD_AS_LIB)
#include "ui/controls/colorpickerwidget.h"
#endif

namespace
{
// The same number-box the inline node editors use (nodes/test.cpp): center
// aligned, +-99999, 2 decimals, 0.1 step, no keyboard tracking.
QDoubleSpinBox* makeNumberBox()
{
	auto box = new QDoubleSpinBox;
	box->setRange(-99999.0, 99999.0);
	box->setDecimals(2);
	box->setSingleStep(0.1);
	box->setAlignment(Qt::AlignCenter);
	box->setKeyboardTracking(false);
	box->setMinimumHeight(22);
	return box;
}

const char* kComponentNames[4] = { "X", "Y", "Z", "W" };
}

NodePropertiesPanel::NodePropertiesPanel(QWidget* parent)
	: QWidget(parent)
{
	buildUi();
}

void NodePropertiesPanel::buildUi()
{
	auto layout = new QVBoxLayout;
	layout->setContentsMargins(0, 0, 0, 0);
	setLayout(layout);

	mStack = new QStackedWidget;
	layout->addWidget(mStack);

	mStack->addWidget(buildSettingsPage(false)); // 0: graph settings
	mStack->addWidget(buildSettingsPage(true));  // 1: master material settings

	// 2: node page
	mNodePage = new QWidget;
	auto nodeLayout = new QVBoxLayout;
	nodeLayout->setContentsMargins(8, 8, 8, 8);
	nodeLayout->setSpacing(6);
	mNodePage->setLayout(nodeLayout);

	mNodeTitle = new QLabel;
	auto titleFont = mNodeTitle->font();
	titleFont.setWeight(QFont::DemiBold);
	titleFont.setPointSizeF(titleFont.pointSizeF() + 1);
	mNodeTitle->setFont(titleFont);
	mNodeType = new QLabel;
	mNodeType->setStyleSheet("color: rgba(200,200,200,.55);");

	nodeLayout->addWidget(mNodeTitle);
	nodeLayout->addWidget(mNodeType);
	nodeLayout->addSpacing(4);

	auto scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	mEditorHost = new QWidget;
	mEditorLayout = new QVBoxLayout;
	mEditorLayout->setContentsMargins(0, 0, 0, 0);
	mEditorLayout->setSpacing(6);
	mEditorLayout->addStretch();
	mEditorHost->setLayout(mEditorLayout);
	scroll->setWidget(mEditorHost);
	nodeLayout->addWidget(scroll);

	mStack->addWidget(mNodePage);
	mStack->setCurrentIndex(0);
}

QWidget* NodePropertiesPanel::buildSettingsPage(bool compact)
{
	SettingsForm& form = compact ? mMasterForm : mGraphForm;

	auto page = new QWidget;
	auto layout = new QVBoxLayout;
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	page->setLayout(layout);

	auto header = new QLabel(compact ? tr("Material Settings") : tr("Graph Settings"));
	auto headerFont = header->font();
	headerFont.setWeight(QFont::DemiBold);
	headerFont.setPointSizeF(headerFont.pointSizeF() + 1);
	header->setFont(headerFont);
	layout->addWidget(header);

	auto sub = new QLabel(compact
		? tr("The selected master node's material")
		: tr("No node selected"));
	sub->setStyleSheet("color: rgba(200,200,200,.55);");
	layout->addWidget(sub);
	layout->addSpacing(4);

	auto formLayout = new QFormLayout;
	formLayout->setLabelAlignment(Qt::AlignLeft);
	formLayout->setFormAlignment(Qt::AlignTop);
	formLayout->setHorizontalSpacing(10);
	formLayout->setVerticalSpacing(6);

	form.name = new QLineEdit;
	formLayout->addRow(tr("Name"), form.name);

	form.blend = new QComboBox;
	form.blend->addItems({ tr("Opaque"), tr("Blend"), tr("Additive") });
	formLayout->addRow(tr("Blend"), form.blend);

	form.cull = new QComboBox;
	form.cull->addItems({ tr("Front"), tr("Back"), tr("None") });
	formLayout->addRow(tr("Cull"), form.cull);

	form.bakeResolution = new QSpinBox;
	form.bakeResolution->setRange(128, 4096);
	form.bakeResolution->setSingleStep(128);
	form.bakeResolution->setValue(1024);
	form.bakeResolution->setKeyboardTracking(false);
	form.bakeResolution->setToolTip(tr("Per-texel bake resolution for this material (final quality)"));
	formLayout->addRow(tr("Bake Resolution"), form.bakeResolution);

	if (!compact) {
		form.renderLayer = new QComboBox;
		form.renderLayer->addItems({ tr("Opaque"), tr("AlphaTested"), tr("Transparent"), tr("Overlay") });
		formLayout->addRow(tr("Render Layer"), form.renderLayer);

		form.zwrite = new QCheckBox;
		formLayout->addRow(tr("Z Write"), form.zwrite);
		form.depthTest = new QCheckBox;
		formLayout->addRow(tr("Depth Test"), form.depthTest);
		form.fog = new QCheckBox;
		formLayout->addRow(tr("Fog"), form.fog);
		form.castShadow = new QCheckBox;
		formLayout->addRow(tr("Cast Shadows"), form.castShadow);
		form.receiveShadow = new QCheckBox;
		formLayout->addRow(tr("Receive Shadows"), form.receiveShadow);
		form.acceptLighting = new QCheckBox;
		formLayout->addRow(tr("Accept Lighting"), form.acceptLighting);
	}

	layout->addLayout(formLayout);
	layout->addStretch();

	// every edit rebuilds the settings struct and hands it to the page
	connect(form.name, &QLineEdit::editingFinished, this, [this]() { emitSettings(); });
	connect(form.blend, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { emitSettings(); });
	connect(form.cull, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { emitSettings(); });
	connect(form.bakeResolution, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { emitSettings(); });
	if (!compact) {
		connect(form.renderLayer, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { emitSettings(); });
		for (auto box : { form.zwrite, form.depthTest, form.fog,
		                  form.castShadow, form.receiveShadow, form.acceptLighting })
			connect(box, &QCheckBox::toggled, this, [this](bool) { emitSettings(); });
	}

	return page;
}

void NodePropertiesPanel::setScene(GraphNodeScene* scene)
{
	if (mScene != nullptr)
		disconnect(mScene, nullptr, this, nullptr);

	mScene = scene;
	if (mScene == nullptr)
		return;

	connect(mScene, &GraphNodeScene::nodeSelected, this, &NodePropertiesPanel::showNode);
	// keep the panel's editors in step with inline-widget and script edits
	connect(mScene, &GraphNodeScene::nodeValueChanged, this,
	        [this](NodeModel* model, int) {
		if (model == mNode && !mUpdating)
			refreshFromNode();
	});
	// a deleted node must not leave a dangling editor behind
	connect(mScene, &GraphNodeScene::nodeRemoved, this, [this](GraphNode*) {
		if (mNode != nullptr && mGraph != nullptr && !mGraph->nodes.contains(mNode->id))
			showNode(nullptr);
	});
}

void NodePropertiesPanel::setGraph(NodeGraph* graph)
{
	mGraph = graph;
	mNode = nullptr;
	refreshSettings();
	mStack->setCurrentIndex(0);
}

NodePropertiesPanel::View NodePropertiesPanel::currentView() const
{
	switch (mStack->currentIndex()) {
	case 1: return View::MaterialSettings;
	case 2: return View::Node;
	default: return View::GraphSettings;
	}
}

void NodePropertiesPanel::refreshSettings()
{
	if (mGraph == nullptr)
		return;
	const MaterialSettings& s = mGraph->settings;

	mUpdating = true;
	for (SettingsForm* form : { &mGraphForm, &mMasterForm }) {
		form->name->setText(s.name);
		form->blend->setCurrentIndex((int)s.blendMode);
		form->cull->setCurrentIndex((int)s.cullMode);
		form->bakeResolution->setValue(s.bakeResolution);
		if (form->renderLayer != nullptr) {
			form->renderLayer->setCurrentIndex((int)s.renderLayer);
			form->zwrite->setChecked(s.zwrite);
			form->depthTest->setChecked(s.depthTest);
			form->fog->setChecked(s.fog);
			form->castShadow->setChecked(s.castShadow);
			form->receiveShadow->setChecked(s.receiveShadow);
			form->acceptLighting->setChecked(s.acceptLighting);
		}
	}
	mUpdating = false;
}

void NodePropertiesPanel::emitSettings()
{
	if (mGraph == nullptr || mUpdating)
		return;

	// start from the live settings so fields absent from the compact form
	// survive a master-view edit
	MaterialSettings s = mGraph->settings;
	const SettingsForm& form = (mStack->currentIndex() == 1) ? mMasterForm : mGraphForm;
	s.name = form.name->text();
	s.blendMode = (BlendMode)form.blend->currentIndex();
	s.cullMode = (CullMode)form.cull->currentIndex();
	s.bakeResolution = form.bakeResolution->value();
	if (form.renderLayer != nullptr) {
		s.renderLayer = (RenderLayer)form.renderLayer->currentIndex();
		s.zwrite = form.zwrite->isChecked();
		s.depthTest = form.depthTest->isChecked();
		s.fog = form.fog->isChecked();
		s.castShadow = form.castShadow->isChecked();
		s.receiveShadow = form.receiveShadow->isChecked();
		s.acceptLighting = form.acceptLighting->isChecked();
	}

	emit settingsEdited(s);
	refreshSettings(); // both forms mirror the (possibly command-adjusted) result
}

void NodePropertiesPanel::showNode(NodeModel* node)
{
	// the master node gets the material settings view, not per-socket editors
	if (node != nullptr && mGraph != nullptr && node == mGraph->getMasterNode()) {
		mNode = node;
		refreshSettings();
		mStack->setCurrentIndex(1);
		return;
	}

	mNode = node;
	if (node == nullptr) {
		refreshSettings();
		mStack->setCurrentIndex(0);
		return;
	}

	rebuildNodeEditors();
	mStack->setCurrentIndex(2);
}

void NodePropertiesPanel::rebuildNodeEditors()
{
	// drop the previous editors (everything but the trailing stretch);
	// hide before deleteLater so findChildren/visibility never see stale rows
	while (mEditorLayout->count() > 1) {
		auto item = mEditorLayout->takeAt(0);
		if (item->widget() != nullptr) {
			item->widget()->hide();
			item->widget()->deleteLater();
		}
		delete item;
	}
	mNumberBoxes.clear();
	mColorSwatch = nullptr;
	mTextureButton = nullptr;

	if (mNode == nullptr)
		return;

	mNodeTitle->setText(mNode->title);
	mNodeType->setText(mNode->typeName);

	const QString& type = mNode->typeName;
	auto value = mNode->serializeWidgetValue();

	auto addRow = [this](const QString& label, QWidget* editor) {
		auto row = new QWidget;
		auto rowLayout = new QHBoxLayout;
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(8);
		row->setLayout(rowLayout);
		auto text = new QLabel(label);
		text->setMinimumWidth(28);
		rowLayout->addWidget(text);
		rowLayout->addWidget(editor, 1);
		mEditorLayout->insertWidget(mEditorLayout->count() - 1, row);
	};

	if (type == "float") {
		auto box = makeNumberBox();
		box->setValue(value.toDouble());
		mNumberBoxes.append(box);
		connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
		        [this](double v) { writeValue(QJsonValue(v)); });
		addRow(tr("Value"), box);
		return;
	}

	if (type == "vector2" || type == "vector3" || type == "vector4") {
		const int count = (type == "vector2") ? 2 : (type == "vector3") ? 3 : 4;
		auto obj = value.toObject();
		static const char* keys[4] = { "x", "y", "z", "w" };
		for (int i = 0; i < count; ++i) {
			auto box = makeNumberBox();
			box->setValue(obj[keys[i]].toDouble());
			mNumberBoxes.append(box);
			connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			        [this, count](double) {
				QJsonObject out;
				static const char* k[4] = { "x", "y", "z", "w" };
				for (int c = 0; c < count && c < mNumberBoxes.size(); ++c)
					out[k[c]] = mNumberBoxes[c]->value();
				writeValue(out);
			});
			addRow(tr(kComponentNames[i]), box);
		}
		return;
	}

#if (EFFECT_BUILD_AS_LIB)
	if (type == "color") {
		auto swatch = new ColorPickerWidget;
		swatch->setFixedHeight(22);
		auto obj = value.toObject();
		QColor col;
		col.setRedF(obj["r"].toDouble());
		col.setGreenF(obj["g"].toDouble());
		col.setBlueF(obj["b"].toDouble());
		col.setAlphaF(obj["a"].toDouble(1.0));
		swatch->setColor(col);
		mColorSwatch = swatch;
		connect(swatch, &ColorPickerWidget::onColorChanged, this, [this](QColor color) {
			QJsonObject out;
			out["r"] = color.redF();
			out["g"] = color.greenF();
			out["b"] = color.blueF();
			out["a"] = color.alphaF();
			writeValue(out);
		});
		addRow(tr("Color"), swatch);
		return;
	}
#endif

	if (type == "texture") {
		auto button = new QPushButton;
		button->setIconSize(QSize(120, 120));
		button->setMinimumSize(140, 126);
		button->setToolTip(tr("Choose an image"));
		button->setStyleSheet("background:rgba(0,0,0,.2); border: 1px solid rgba(50,50,50,.4);");
		auto texNode = static_cast<TextureNode*>(mNode);
		const auto path = texNode->getTexturePath();
		if (!path.isEmpty())
			button->setIcon(QIcon(path));
		mTextureButton = button;
		connect(button, &QPushButton::clicked, this, &NodePropertiesPanel::pickTextureForNode);
		mEditorLayout->insertWidget(mEditorLayout->count() - 1, button);
		return;
	}

	// everything else edits inline on the node (combos, previews) or has no
	// value at all — be honest about it
	auto info = new QLabel(mNode->inSockets.isEmpty() && mNode->outSockets.isEmpty()
		? tr("This node has no editable values.")
		: tr("This node has no panel-editable values; its controls live on the node body."));
	info->setWordWrap(true);
	info->setStyleSheet("color: rgba(200,200,200,.55);");
	mEditorLayout->insertWidget(mEditorLayout->count() - 1, info);
}

void NodePropertiesPanel::pickTextureForNode()
{
	if (mNode == nullptr || mNode->typeName != "texture")
		return;

	auto filename = QFileDialog::getOpenFileName(this, tr("Choose an image"));
	if (filename.isEmpty())
		return;

	// DB-backed route when a project database is behind the TextureManager
	// (same flow the old texture property rows used); plain path otherwise.
	QString stored = filename;
	if (TextureManager::getSingleton()->hasDatabase()) {
		auto tex = TextureManager::getSingleton()->importTexture(filename);
		if (tex != nullptr && !tex->guid.isEmpty())
			stored = tex->guid;
	}

	writeValue(QJsonValue(stored));
	refreshFromNode();
}

void NodePropertiesPanel::writeValue(const QJsonValue& value)
{
	if (mNode == nullptr || mUpdating)
		return;

	// ONE write path: deserializeWidgetValue (updates the node's inline
	// widget too), then a single valueChanged so the scene invalidates the
	// graph exactly once. blockSignals keeps widget-triggered re-emissions
	// from doubling up.
	mUpdating = true;
	mNode->blockSignals(true);
	mNode->deserializeWidgetValue(value);
	mNode->blockSignals(false);
	mNode->notifyValueChanged(0);
	mUpdating = false;
}

void NodePropertiesPanel::refreshFromNode()
{
	if (mNode == nullptr)
		return;

	mUpdating = true;
	auto value = mNode->serializeWidgetValue();
	const QString& type = mNode->typeName;

	if (type == "float" && mNumberBoxes.size() == 1) {
		mNumberBoxes[0]->blockSignals(true);
		mNumberBoxes[0]->setValue(value.toDouble());
		mNumberBoxes[0]->blockSignals(false);
	}
	else if (type.startsWith("vector") && !mNumberBoxes.isEmpty()) {
		auto obj = value.toObject();
		static const char* keys[4] = { "x", "y", "z", "w" };
		for (int i = 0; i < mNumberBoxes.size() && i < 4; ++i) {
			mNumberBoxes[i]->blockSignals(true);
			mNumberBoxes[i]->setValue(obj[keys[i]].toDouble());
			mNumberBoxes[i]->blockSignals(false);
		}
	}
#if (EFFECT_BUILD_AS_LIB)
	else if (type == "color" && mColorSwatch != nullptr) {
		auto obj = value.toObject();
		QColor col;
		col.setRedF(obj["r"].toDouble());
		col.setGreenF(obj["g"].toDouble());
		col.setBlueF(obj["b"].toDouble());
		col.setAlphaF(obj["a"].toDouble(1.0));
		mColorSwatch->setColor(col);
	}
#endif
	else if (type == "texture" && mTextureButton != nullptr) {
		auto path = static_cast<TextureNode*>(mNode)->getTexturePath();
		mTextureButton->setIcon(path.isEmpty() ? QIcon() : QIcon(path));
	}
	mUpdating = false;
}
