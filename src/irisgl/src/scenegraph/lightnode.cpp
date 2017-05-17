#include "lightnode.h"
#include "../core/scenenode.h"
#include "../animation/animableproperty.h"
#include "../animation/keyframeset.h"
#include "../animation/animation.h"
#include "../animation/propertyanim.h"
#include "../materials/propertytype.h"

namespace iris
{

QList<Property*> LightNode::getProperties()
{
    auto props = QList<Property*>();

    auto colorProp = new ColorProperty();
    colorProp->displayName = "Light Color";
    colorProp->name = "lightColor";
    colorProp->value = color;
    props.append(colorProp);

    auto prop = new FloatProperty();
    prop->displayName = "Intensity";
    prop->name = "intensity";
    prop->value = intensity;
    props.append(prop);

    prop = new FloatProperty();
    prop->displayName = "Distance";
    prop->name = "distance";
    prop->value = distance;
    props.append(prop);

    prop = new FloatProperty();
    prop->displayName = "Spot CutOff";
    prop->name = "spotCutOff";
    prop->value = spotCutOff;
    props.append(prop);

    prop = new FloatProperty();
    prop->displayName = "Spot CutOff Softness";
    prop->name = "spotCutOffSoftness";
    prop->value = spotCutOffSoftness;
    props.append(prop);

    return props;
}

QVariant LightNode::getPropertyValue(QString valueName)
{
    if(valueName == "intensity")
        return intensity;
    if(valueName == "lightColor")
        return color;
    if(valueName == "distance")
        return distance;
    if(valueName == "spotCutOff")
        return spotCutOff;
    if(valueName == "spotCutOffSoftness")
        return spotCutOffSoftness;

    return SceneNode::getPropertyValue(valueName);
}

void LightNode::updateAnimation(float time)
{
    if (!!animation) {
        if(animation->hasPropertyAnim("Intensity"))
            intensity = animation->getFloatPropertyAnim("Intensity")->getValue(time);
        if(animation->hasPropertyAnim("LightColor"))
            color = animation->getColorPropertyAnim("LightColor")->getValue(time);
        if(animation->hasPropertyAnim("Distance"))
            distance = animation->getFloatPropertyAnim("Distance")->getValue(time);
        if(animation->hasPropertyAnim("SpotCutOff"))
            spotCutOff = animation->getFloatPropertyAnim("SpotCutOff")->getValue(time);
        if(animation->hasPropertyAnim("SpotCutOffSoftness"))
            spotCutOffSoftness = animation->getFloatPropertyAnim("SpotCutOffSoftness")->getValue(time);
    }

    SceneNode::updateAnimation(time);
}



}
