#ifndef PROPERTYTYPE_H
#define PROPERTYTYPE_H

enum PropertyType
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
    unsigned id;
    QString displayName;
    QString name;
    PropertyType type;

    virtual QVariant getValue() = 0;
};

#endif // PROPERTYTYPE_H
