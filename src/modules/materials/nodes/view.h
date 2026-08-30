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
#include "../propertywidgets/propertywidgetbase.h"

/* Converts vec3 from world space to screen space
	range is 0 to 1
*/
class WorldToScreenSpaceNode : public NodeModel
{
public:
	WorldToScreenSpaceNode();

	virtual void process(ModelContext* context) override;
};

/* Returns screen space position for fragment
range is 0 to 1
*/
class ScreenPosNode : public NodeModel
{
public:
	ScreenPosNode();

	virtual void process(ModelContext* context) override;
};

/* View direction for camera */
class ViewDirNode : public NodeModel
{
public:
	ViewDirNode();

	virtual void process(ModelContext* context) override;
};

/* Camera world space position */
class CameraPosNode : public NodeModel
{
public:
	CameraPosNode();

	virtual void process(ModelContext* context) override;
};