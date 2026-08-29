#ifndef NODE_MODEL_H
#define NODE_MODEL_H

#include <QObject>
#include <QJsonValue>
#include <QVector>
#include <QColor>
#include <QIcon>
#include <QWidget>

#include "socketmodel.h"

enum class NodeCategory {
	Input = 0,
	Math = 1,
	Constants = 2,
	Texture = 3,
	Vector = 4,
	Object = 5,
	Utility = 6,
	PlaceHolder = 7,
	Properties = 8
};

class QWidget;
class ModelContext;

class NodeModel : public QObject
{
	Q_OBJECT

private :

	float x;
	float y;
public:
	QString id;

	QVector<SocketModel*> inSockets;
	QVector<SocketModel*> outSockets;

	QString typeName;
	QString title;
	NodeCategory nodeType;

	QWidget* widget;
	QColor titleColor;
	QIcon icon;
	QString iconPath;

	void setX(float val) { x = val; }
	float getX() { return x; }
	
	void setY(float val) { y = val; }
	float getY() { return y; }

	bool enablePreview;
	bool isPreviewEnabled()
	{
		return enablePreview;

	}
	NodeModel();

	NodeGraph* graph = nullptr;

	void updateStyle();
	void addInputSocket(SocketModel* sock);
	void addOutputSocket(SocketModel* sock);
	void setWidget(QWidget *wid);

	virtual QString getSocketValue(int socketIndex, ModelContext* context)
	{
		return outSockets[socketIndex]->getValue();
	}

	QString getValueFromInputSocket(int index);
	QString getOutputSocketVarName(int index);


	virtual void preProcess(ModelContext* context) {}
	virtual void process(ModelContext* context) {}
	virtual void postProcess(ModelContext* context) {}
	virtual QString generatePreview(ModelContext* context)
	{
		return "";
	}

	NodeGraph *getGraph() const;
	void setGraph(NodeGraph *value);
	QColor setNodeTitleColor();
	void setNodeType(NodeCategory type);

	// gets socket from both in and out sockets
	// return nullptr if sockets isnt found
	SocketModel* getSocketById(const QString& sockId);

	virtual QJsonValue serializeWidgetValue(int widgetIndex = 0)
	{
		return "";
	}

	virtual void deserializeWidgetValue(QJsonValue val, int widgetIndex = 0)
	{

	}

	static QString getEnumString(NodeCategory type);

signals:
	void valueChanged(NodeModel*, int sockedIndex);
	void titleColorChanged();

protected:
	virtual NodeModel* createDuplicate()
	{
		return new NodeModel();
	}
};

#endif// NODE_MODEL_H