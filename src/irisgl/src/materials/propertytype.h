#ifndef PROPERTYTYPE_H
#define PROPERTYTYPE_H

#include <QVariant>
#include <QColor>
#include <QVector2D>
#include <QVector3D>

namespace iris
{

enum class PropertyType
{
    None,
    Bool,
    Int,
    Float,
    Vec2,
    Vec3,
    Color,
    Texture,
    File,
    List
};

struct Property
{
    unsigned            id;
    QString             displayName;
    QString             name;
    PropertyType        type;

    virtual QVariant    getValue() = 0;
};

class PropertyListener
{
public:
    virtual void onPropertyChanged(Property*) = 0;
};

struct BoolProperty : public Property
{
    bool value;

    BoolProperty () {
        type = PropertyType::Bool;
    }

    QVariant getValue() {
        return value;
    }
};

struct IntProperty : public Property
{
    int value;
    int minValue;
    int maxValue;

    IntProperty() {
        type = PropertyType::Int;
    }

    QVariant getValue() {
        return value;
    }
};

struct FloatProperty : public Property
{
    float value;
    float minValue;
    float maxValue;

    FloatProperty() {
        type = PropertyType::Float;
    }

    QVariant getValue() {
        return value;
    }
};

struct ColorProperty : public Property
{
    QColor value;

    ColorProperty () {
        type = PropertyType::Color;
    }

    QVariant getValue() {
        return value;
    }
};

struct TextureProperty : public Property
{
    QString value;

    TextureProperty () {
        type = PropertyType::Texture;
    }

    QVariant getValue() {
        return value;
    }
};

struct FileProperty : public Property
{
    QString value;
    QString suffix;

    FileProperty () {
        type = PropertyType::File;
    }

    QVariant getValue() {
        return value;
    }
};

struct ListProperty : public Property
{
    QStringList value;
    int index;

    ListProperty () {
        type = PropertyType::List;
    }

    QVariant getValue() {
        return value;
    }
};

/* more abstract types without a physical widget */

struct Vec2Property : public Property
{
    QVector2D value;

    Vec2Property() {
        type = PropertyType::Vec2;
    }

    QVariant getValue() {
        return value;
    }
};

struct Vec3Property : public Property
{
    QVector3D value;

    Vec3Property() {
        type = PropertyType::Vec3;
    }

    QVariant getValue() {
        return value;
    }
};

}

#endif // PROPERTYTYPE_H
