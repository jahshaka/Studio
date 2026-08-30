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



class WorldToObjectNode : public NodeModel
{
public:
	WorldToObjectNode();

	virtual void process(ModelContext* context) override;
};

class DepthNode : public NodeModel
{
public:
	DepthNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class FresnelNode : public NodeModel
{
public:
	FresnelNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class ObjectDimensionsNode : public NodeModel
{
public:
	ObjectDimensionsNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};