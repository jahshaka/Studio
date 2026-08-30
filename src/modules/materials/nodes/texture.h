#pragma once

#include <QLineEdit>
#include <QComboBox>
#include <QDoubleValidator>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../graph/sockets.h"
#include "../generator/shadercontext.h"
#include "../propertywidgets/propertywidgetbase.h"

/* Blends two normals */
class CombineNormalsNode : public NodeModel
{
public:
	CombineNormalsNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

/* Provides the texture width, height, 1/width and 1/height */
class TexelSizeNode : public NodeModel
{
public:
	TexelSizeNode();

	virtual void process(ModelContext* context) override;
};

/*
// Samples texture
class SampleTextureNode : public NodeModel
{
public:
	SampleTextureNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};
*/
/*
// Samples texture as latlong, requires vec3 UVs
class SampleEquirectangularTextureNode : public NodeModel
{
public:
	SampleEquirectangularTextureNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};
*/
/* Generates UV based on flipbook animation
	Inputs: UV, rows, columns, startframe
	Output: UV
*/

class FlipbookUVAnimationNode : public NodeModel
{
public:
	FlipbookUVAnimationNode();

	virtual void process(ModelContext* context) override;
};

class TileUVNode : public NodeModel
{
public:
	TileUVNode();

	virtual void process(ModelContext* context) override;
};