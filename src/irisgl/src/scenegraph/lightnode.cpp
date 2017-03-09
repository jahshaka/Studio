#include "lightnode.h"
#include "../core/scenenode.h"
#include "../animation/animableproperty.h"

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
    if(valueName == "LightIntensity")
        return intensity;

    return SceneNode::getAnimPropertyValue(valueName);
}

}
