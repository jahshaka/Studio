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
#include "../propertywidgets/propertywidgetbase.h"

/* Reflects vector given normal and incident ray
Input: normal, incident
Output: reflected ray
*/
class ReflectVectorNode : public NodeModel
{
public:
	ReflectVectorNode();

};

class SplitVectorNode : public NodeModel
{
public:
	SplitVectorNode();

};

class ComposeVectorNode : public NodeModel
{
public:
	ComposeVectorNode();

};

class DistanceVectorNode : public NodeModel
{
public:
	DistanceVectorNode();

};

class DotVectorNode : public NodeModel
{
public:
	DotVectorNode();

};

class LengthVectorNode : public NodeModel
{
public:
	LengthVectorNode();

};

class NormalizeVectorNode : public NodeModel
{
public:
	NormalizeVectorNode();

};