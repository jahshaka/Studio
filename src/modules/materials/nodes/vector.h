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

/* Reflects vector given normal and incident ray
Input: normal, incident
Output: reflected ray
*/
class ReflectVectorNode : public NodeModel
{
public:
	ReflectVectorNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class SplitVectorNode : public NodeModel
{
public:
	SplitVectorNode();

	virtual void process(ModelContext* context) override;
};

class ComposeVectorNode : public NodeModel
{
public:
	ComposeVectorNode();

	virtual void process(ModelContext* context) override;
};

class DistanceVectorNode : public NodeModel
{
public:
	DistanceVectorNode();

	virtual void process(ModelContext* context) override;
};

class DotVectorNode : public NodeModel
{
public:
	DotVectorNode();

	virtual void process(ModelContext* context) override;
};

class LengthVectorNode : public NodeModel
{
public:
	LengthVectorNode();

	virtual void process(ModelContext* context) override;
};

class NormalizeVectorNode : public NodeModel
{
public:
	NormalizeVectorNode();

	virtual void process(ModelContext* context) override;
};