#pragma once

#include <QLineEdit>
#include <QComboBox>
#include <QDoubleValidator>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

#include "../graph/nodegraph.h"
#include "../graph/nodemodel.h"
#include "../graph/sockets.h"
#include "../generator/shadercontext.h"


class PannerNode : public NodeModel
{
public:
	PannerNode();
	virtual void process(ModelContext* ctx) override;
};