/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "irisgl/core/math/vec.h"
#include "texture.h"

/*    COMBINE NORMAL    */
CombineNormalsNode::CombineNormalsNode()
{
	setNodeType(NodeCategory::Texture);
	title = "Combine Normals";
	typeName = "combinenormals";
	enablePreview = true;

	addInputSocket(new Vector3SocketModel("NormalA", "vec3(0.0, 0.0, 1.0)"));
	addInputSocket(new Vector3SocketModel("NormalB", "vec3(0.0, 0.0, 1.0)"));
	addOutputSocket(new Vector3SocketModel("Result"));
}


TexelSizeNode::TexelSizeNode()
{
	setNodeType(NodeCategory::Texture);
	title = "Texel Size";
	typeName = "texelsize";
	enablePreview = true;

	addInputSocket(new TextureSocketModel("Texture"));
	addOutputSocket(new Vector2SocketModel("Size"));
	addOutputSocket(new FloatSocketModel("Width"));
	addOutputSocket(new FloatSocketModel("Height"));
	addOutputSocket(new FloatSocketModel("1/Width"));
	addOutputSocket(new FloatSocketModel("1/Height"));
	// Appended (serialization-compatible — existing saves reference outs
	// 0..4 only): the aspect-ratio outs (IMAGE_PLANE_SPEC option C.2). W/H
	// drives the Unreal-style ratio chain without a divide/compose detour.
	addOutputSocket(new FloatSocketModel("Aspect"));
	addOutputSocket(new FloatSocketModel("1/Aspect"));
}


/*
SampleEquirectangularTextureNode::SampleEquirectangularTextureNode()
{
	setNodeType(NodeType::Math);
	title = "Sample Texture Equirectangular";
	typeName = "texelsize";
	enablePreview = true;

	addInputSocket(new TextureSocketModel("Texture"));
	addOutputSocket(new Vector2SocketModel("Size"));
	addOutputSocket(new FloatSocketModel("Width"));
	addOutputSocket(new FloatSocketModel("Height"));
	addOutputSocket(new FloatSocketModel("1/Width"));
	addOutputSocket(new FloatSocketModel("1/Height"));
}
*/


/*    THE UV NODE (MATERIAL_UV_NODES_SPEC D-3)    */
UVNode::UVNode()
{
	setNodeType(NodeCategory::Input);
	title = "UV";
	typeName = "uv";
	enablePreview = true;

	// SOCKET INDICES ARE FROZEN: 0/1/2 are `uvTransform`'s three inputs and
	// out 0 is both aliases' only output. Rotation is APPENDED at 3 (a new
	// index no saved file can reference), exactly the rule that made the
	// texelsize aspect outs free.
	addInputSocket(new Vector2SocketModel("UV", "v_texCoord"));
	addInputSocket(new Vector2SocketModel("Tiling", "vec2(1.0, 1.0)"));
	addInputSocket(new Vector2SocketModel("Offset", "vec2(0.0, 0.0)"));
	addInputSocket(new FloatSocketModel("Rotation", "0.0"));
	addOutputSocket(new Vector2SocketModel("UV"));

	// Inline editors, Vector2Node's card conventions: labelled rows of
	// spinboxes writing the socket DEFAULTS (pushSocketDefaults), so a
	// connected socket overrides them.
	auto wid = new QWidget;
	auto rows = new QVBoxLayout;
	wid->setLayout(rows);
	wid->setFixedWidth(158); // the expanding boxes otherwise overrun the 170px card
	rows->setContentsMargins(4, 2, 4, 2);
	rows->setSpacing(2);

	tileXBox = new QDoubleSpinBox;
	tileYBox = new QDoubleSpinBox;
	offsetXBox = new QDoubleSpinBox;
	offsetYBox = new QDoubleSpinBox;
	rotationBox = new QDoubleSpinBox;
	for (auto box : { tileXBox, tileYBox, offsetXBox, offsetYBox, rotationBox }) {
		box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		box->setAlignment(Qt::AlignCenter);
		box->setRange(-99999.0, 99999.0);
		box->setDecimals(2);
		box->setSingleStep(0.1);
		box->setFixedHeight(20);
		box->setKeyboardTracking(false);
	}
	// TILING DEFAULTS TO 1, VISIBLY (D-3): the node reads as "the mesh's UVs"
	// until the user changes a number, which is what makes one node able to
	// replace the bare-coordinate node it absorbed.
	tileXBox->setValue(1.0);
	tileYBox->setValue(1.0);
	rotationBox->setSingleStep(5.0);
	rotationBox->setRange(-360.0, 360.0);

	uvSetCombo = new QComboBox();
	uvSetCombo->addItem("UV 0");
	uvSetCombo->addItem("UV 1");
	uvSetCombo->addItem("UV 2");
	uvSetCombo->addItem("UV 3");
	uvSetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	uvSetCombo->setFixedHeight(20);

	auto addPairRow = [&](const QString &name, QWidget *x, QWidget *y) {
		auto row = new QHBoxLayout;
		row->setContentsMargins(0, 0, 0, 0);
		row->setSpacing(3);
		auto label = new QLabel(name);
		label->setFixedWidth(38);
		row->addWidget(label);
		row->addWidget(x);
		if (y) row->addWidget(y);
		rows->addLayout(row);
	};
	addPairRow("Tile", tileXBox, tileYBox);
	addPairRow("Offset", offsetXBox, offsetYBox);
	addPairRow("Rot", rotationBox, nullptr);
	addPairRow("Set", uvSetCombo, nullptr);

	widget = wid;
	widget->setStyleSheet(
		"QDoubleSpinBox{border: 2px solid rgba(200, 200, 200, .4); padding: 2px; background: rgba(0, 0, 0, 0.2);}"
		"QComboBox{border: 2px solid rgba(200, 200, 200, .4); padding: 1px; background: rgba(0, 0, 0, 0.2);}"
		"QWidget{ background: rgba(0,0,0,0); color: rgba(250,250,250,1); }"
		"QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow { width: 0; height:0;}"
		"QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; height:0;}"
	);

	auto onEdit = [this](double) {
		tiling = iris::Vec2(float(tileXBox->value()), float(tileYBox->value()));
		offset = iris::Vec2(float(offsetXBox->value()), float(offsetYBox->value()));
		rotation = float(rotationBox->value());
		pushSocketDefaults();
		emit valueChanged(this, 0);
	};
	for (auto box : { tileXBox, tileYBox, offsetXBox, offsetYBox, rotationBox })
		connect(box, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), onEdit);
	connect(uvSetCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
	        [this](int index) {
		        // PERSISTED, unlike the combo it replaces: TextureCoordinateNode
		        // serialized nothing, so the choice reset to TexCoord0 on every
		        // load (MATERIAL_UV_NODES_SPEC §1.1, defect 1).
		        uvSet = qBound(0, index, 3);
		        emit valueChanged(this, 0);
	        });

	pushSocketDefaults();
}

void UVNode::pushSocketDefaults()
{
	// The editors ARE the unconnected-socket values: the compiler reads the
	// GLSL default string when nothing is connected (bakeprogram makeRef).
	inSockets[1]->setValue(QStringLiteral("vec2(%1, %2)")
	                           .arg(double(tiling.x())).arg(double(tiling.y())));
	inSockets[2]->setValue(QStringLiteral("vec2(%1, %2)")
	                           .arg(double(offset.x())).arg(double(offset.y())));
	inSockets[3]->setValue(QString::number(double(rotation)));
}

QJsonValue UVNode::serializeWidgetValue(int widgetIndex)
{
	Q_UNUSED(widgetIndex);
	QJsonObject obj;
	obj["tileX"] = double(tiling.x());
	obj["tileY"] = double(tiling.y());
	obj["offsetX"] = double(offset.x());
	obj["offsetY"] = double(offset.y());
	obj["rotation"] = double(rotation);
	obj["uvSet"] = uvSet;
	return obj;
}

void UVNode::deserializeWidgetValue(QJsonValue val, int widgetIndex)
{
	Q_UNUSED(widgetIndex);
	// TOLERANT-ABSENT (spec phase 1): a `uvTransform` saved before rotation
	// existed has no "rotation"/"uvSet"; a `texCoords` saved anything at all
	// (its combo was never serialized) — including a bare string or null.
	// Every missing field takes the identity value.
	const auto obj = val.toObject();
	tiling = iris::Vec2(float(obj["tileX"].toDouble(1.0)), float(obj["tileY"].toDouble(1.0)));
	offset = iris::Vec2(float(obj["offsetX"].toDouble(0.0)), float(obj["offsetY"].toDouble(0.0)));
	rotation = float(obj["rotation"].toDouble(0.0));
	uvSet = qBound(0, obj["uvSet"].toInt(0), 3);
	// setValue triggers the connected onEdit, which re-derives members and
	// pushes the socket defaults; block signals to set them all atomically.
	for (auto box : { tileXBox, tileYBox, offsetXBox, offsetYBox, rotationBox })
		box->blockSignals(true);
	uvSetCombo->blockSignals(true);
	tileXBox->setValue(tiling.x());
	tileYBox->setValue(tiling.y());
	offsetXBox->setValue(offset.x());
	offsetYBox->setValue(offset.y());
	rotationBox->setValue(rotation);
	uvSetCombo->setCurrentIndex(uvSet);
	for (auto box : { tileXBox, tileYBox, offsetXBox, offsetYBox, rotationBox })
		box->blockSignals(false);
	uvSetCombo->blockSignals(false);
	pushSocketDefaults();
}


FlipbookUVAnimationNode::FlipbookUVAnimationNode()
{
	setNodeType(NodeCategory::Texture);
	title = "Flipbook Animation";
	typeName = "flipbook";
	enablePreview = true;

	addInputSocket(new Vector2SocketModel("UV", "v_texCoord"));
	addInputSocket(new FloatSocketModel("Rows", "1.0"));
	addInputSocket(new FloatSocketModel("Columns", "1.0"));
	addInputSocket(new FloatSocketModel("Animation Length", "2.0"));
	addInputSocket(new FloatSocketModel("Time", "u_time"));

	addOutputSocket(new Vector2SocketModel("UV"));
}

