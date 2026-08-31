/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROPERTYANIMFACTORY_H
#define PROPERTYANIMFACTORY_H

#include <QString>

#include "irisgl/core/properties/property.h"
#include "irisgl/document/animation/propertyanim.h"

// The keyframe timeline can only build a track for the three property types
// that have a PropertyAnim subclass. The document nodes reflect bool/int/
// string/texture/vec2/vec4 fields too (name, visible, lightType, meshPath...),
// and before this factory existed the unhandled types fell through a
// `Q_ASSERT(false)` default onto an *uninitialised* PropertyAnim* — a no-op
// assert plus an indeterminate dereference in release builds.
//
// One list, three consumers: the menu filter (AnimationWidget::
// buildPropertiesMenu), the track factory (createPropertyAnim) and the key
// writer (addPropertyKey). Keeping them expressed in terms of the same
// predicate is what stops them drifting apart again.
//
// Global namespace, matching the rest of src/ (the only namespaced Studio code
// is the materials module).

//! true for the property types the timeline can animate.
inline bool isAnimatablePropertyType(iris::PropertyType type)
{
    return type == iris::PropertyType::Float ||
           type == iris::PropertyType::Vec3  ||
           type == iris::PropertyType::Color;
}

//! Builds the PropertyAnim for `type`, named `name`.
//! Returns nullptr — never an indeterminate pointer — for every type
//! isAnimatablePropertyType() rejects. Callers own the returned object.
inline iris::PropertyAnim *makePropertyAnim(iris::PropertyType type, const QString &name)
{
    iris::PropertyAnim *anim = nullptr;

    switch (type) {
    case iris::PropertyType::Float:
        anim = new iris::FloatPropertyAnim();
        break;

    case iris::PropertyType::Vec3:
        anim = new iris::Vector3DPropertyAnim();
        break;

    case iris::PropertyType::Color:
        anim = new iris::ColorPropertyAnim();
        break;

    default:
        // Unsupported property type: no track exists for it.
        return nullptr;
    }

    anim->setName(name);
    return anim;
}

#endif // PROPERTYANIMFACTORY_H
