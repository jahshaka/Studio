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

class AddNode : public NodeModel
{
public:
	AddNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class SubtractNode : public NodeModel
{
public:
	SubtractNode();

	virtual void process(ModelContext* context) override;
    virtual QString generatePreview(ModelContext* context) override;
};

class MultiplyNode : public NodeModel
{
public:
	MultiplyNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class DivideNode : public NodeModel
{
public:
	DivideNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class PowerNode : public NodeModel
{
public:
	PowerNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class SqrtNode : public NodeModel
{
public:
	SqrtNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class MinNode : public NodeModel
{
public:
	MinNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class MaxNode : public NodeModel
{
public:
	MaxNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class AbsNode : public NodeModel
{
public:
	AbsNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class SignNode : public NodeModel
{
public:
	SignNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class CeilNode : public NodeModel
{
public:
	CeilNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class FloorNode : public NodeModel
{
public:
	FloorNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class RoundNode : public NodeModel
{
public:
	RoundNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};


class TruncNode : public NodeModel
{
public:
	TruncNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class StepNode : public NodeModel
{
public:
	StepNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class SmoothStepNode : public NodeModel
{
public:
	SmoothStepNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class FracNode : public NodeModel
{
public:
	FracNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class ClampNode : public NodeModel
{
public:
	ClampNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class LerpNode : public NodeModel
{
public:
	LerpNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};
/*
class PosterizeNode : public NodeModel
{
public:
	PosterizeNode();

	virtual void process(ModelContext* context) override;
};
*/
class OneMinusNode : public NodeModel
{
public:
	OneMinusNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

class NegateNode : public NodeModel
{
public:
	NegateNode();

	virtual void process(ModelContext* context) override;
	virtual QString generatePreview(ModelContext* context) override;
};

// todo: trig functions
