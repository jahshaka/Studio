#ifndef PROPERTYLISTWIDGET_H
#define PROPERTYLISTWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QUndoStack>
#include "../core/undoredo.h"

namespace Ui {
class PropertyListWidget;
}

class QVBoxLayout;
class NodeGraph;
class FloatProperty;
class Vec2Property;
class Vec3Property;
class Vec4Property;
class IntProperty;
class TextureProperty;
class BasePropertyWidget;
class TexturePropertyWidget;
class PropertyListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PropertyListWidget(QWidget *parent = 0);
    ~PropertyListWidget();

    void addProperty(QWidget* widget);
    void setNodeGraph(NodeGraph* graph);
    void clearPropertyList();
	void setStack(QUndoStack *);

	void setCount(int val);
	int getCount();
	NodeGraph* graph;
	QVector<BasePropertyWidget*> referenceList;
	QVector<Property*> propertyList;
	BasePropertyWidget *currentWidget = Q_NULLPTR;
private:
    void addNewFloatProperty();
	void addFloatProperty(FloatProperty* floatProp);

	void addNewVec2Property();
	void addVec2Property(Vec2Property* vec2Prop);

	void addNewVec3Property();
	void addVec3Property(Vec3Property* vec3Prop);

	void addNewVec4Property();
	void addVec4Property(Vec4Property* vec4Prop);

	void addNewIntProperty();
	void addIntProperty(IntProperty* intProp);
	
	void addNewTextureProperty();
	void addTextureProperty(TextureProperty* texProp, bool requestTextureFromDatabase = false);

    void addToPropertyListWidget(BasePropertyWidget *widget);

	QString generatePropName();
	bool graphHasProperty(QString propName);

private:
    QVBoxLayout* layout;
	int added = 0;
	QFont font;
	QUndoStack *stack;

signals:
	void nameChanged(QString name, QString id);
	void texturePicked(QString filename, TexturePropertyWidget *widget);
	void deleteProperty(QString propID);
	QString imageRequestedForTexture(QString guid);
};

#endif // PROPERTYLISTWIDGET_H
