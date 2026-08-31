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




class DepthNode : public NodeModel
{
public:
	DepthNode();

};

class FresnelNode : public NodeModel
{
public:
	FresnelNode();

};

