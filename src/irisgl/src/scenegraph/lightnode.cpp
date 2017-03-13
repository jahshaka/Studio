#include "lightnode.h"
#include "../core/scenenode.h"
#include "../animation/animableproperty.h"
#include "../animation/keyframeset.h"
#include "../animation/animation.h"
#include "../animation/propertyanim.h"

namespace iris
{

QList<AnimableProperty> LightNode::getAnimableProperties()
{
    auto props = SceneNode::getAnimableProperties();
    props.append(AnimableProperty("LightColor", AnimablePropertyType::Color));
    props.append(AnimableProperty("Intensity", AnimablePropertyType::Float));
    props.append(AnimableProperty("Distance", AnimablePropertyType::Float));
    props.append(AnimableProperty("SpotCutOff", AnimablePropertyType::Float));
    props.append(AnimableProperty("SpotCutOffSoftness", AnimablePropertyType::Float));

    return props;
}

QVariant LightNode::getAnimPropertyValue(QString valueName)
{
    if(valueName == "Intensity")
        return intensity;
    if(valueName == "LightColor")
        return color;
    if(valueName == "Distance")
        return distance;
    if(valueName == "SpotCutOff")
        return spotCutOff;
    if(valueName == "SpotCutOffSoftness")
        return spotCutOffSoftness;

    return SceneNode::getAnimPropertyValue(valueName);
}

void LightNode::updateAnimation(float time)
{
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

    SceneNode::updateAnimation(time);
}



}
