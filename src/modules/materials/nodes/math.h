#include <QLineEdit>
#include <QComboBox>
#include <QDoubleValidator>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "../graph/nodegraph.h"
#include "../models/nodemodel.h"
#include "../graph/sockets.h"

class AddNode : public NodeModel
{
public:
	AddNode();

};

class SubtractNode : public NodeModel
{
public:
	SubtractNode();

};

class MultiplyNode : public NodeModel
{
public:
	MultiplyNode();

};

class DivideNode : public NodeModel
{
public:
	DivideNode();

};

class PowerNode : public NodeModel
{
public:
	PowerNode();

};

class SqrtNode : public NodeModel
{
public:
	SqrtNode();

};

class MinNode : public NodeModel
{
public:
	MinNode();

};

class MaxNode : public NodeModel
{
public:
	MaxNode();

};

class AbsNode : public NodeModel
{
public:
	AbsNode();

};

class SignNode : public NodeModel
{
public:
	SignNode();

};

class CeilNode : public NodeModel
{
public:
	CeilNode();

};

class FloorNode : public NodeModel
{
public:
	FloorNode();

};

class RoundNode : public NodeModel
{
public:
	RoundNode();

};


class TruncNode : public NodeModel
{
public:
	TruncNode();

};

class StepNode : public NodeModel
{
public:
	StepNode();

};

class SmoothStepNode : public NodeModel
{
public:
	SmoothStepNode();

};

class FracNode : public NodeModel
{
public:
	FracNode();

};

class ClampNode : public NodeModel
{
public:
	ClampNode();

};

class LerpNode : public NodeModel
{
public:
	LerpNode();

};
/*
class PosterizeNode : public NodeModel
{
public:
	PosterizeNode();

};
*/
class OneMinusNode : public NodeModel
{
public:
	OneMinusNode();

};

class NegateNode : public NodeModel
{
public:
	NegateNode();

};

// todo: trig functions
