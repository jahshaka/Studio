#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>

#include "ssaopostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
#include "../graphics/texture2d.h"
#include "../materials/propertytype.h"

namespace iris
{

SSAOPostProcess::SSAOPostProcess()
{
    name = "ssao";
    displayName = "SSAO";
    shader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/ssao.fs");

    normals = Texture2D::load(":assets/textures/random_normal.png");

    samples = 5;
    rings = 5;

    radius = 0.2;

    diffArea = 0.5;
    gaussBellCenter = 0.4;

    lumInfluence = 0.8;
}

QList<Property *> SSAOPostProcess::getProperties()
{
    auto props = QList<Property*>();

    auto intProp = new IntProperty();
    intProp->displayName = "Samples";
    intProp->name = "samples";
    intProp->value = samples;
    props.append(intProp);

    intProp = new IntProperty();
    intProp->displayName = "Rings";
    intProp->name = "rings";
    intProp->value = rings;
    props.append(intProp);

    auto prop = new FloatProperty();
    prop->displayName = "Radius";
    prop->name = "radius";
    prop->value = radius;
    prop->minValue = 0.0f;
    prop->maxValue = 1.0f;
    props.append(prop);

    prop = new FloatProperty();
    prop->displayName = "Self-Shadowing Reduction";
    prop->name = "diffArea";
    prop->value = diffArea;
    prop->minValue = 0.0f;
    prop->maxValue = 1.0f;
    props.append(prop);

    // this should probable remain internal or have a different name
    prop = new FloatProperty();
    prop->displayName = "Gauss Bell Center";
    prop->name = "gaussBellCenter";
    prop->value = gaussBellCenter;
    prop->minValue = 0.0f;
    prop->maxValue = 1.0f;
    props.append(prop);

    prop = new FloatProperty();
    prop->displayName = "Luminance Influence";
    prop->name = "lumInfluence";
    prop->value = lumInfluence;
    prop->minValue = 0.0f;
    prop->maxValue = 1.0f;
    props.append(prop);

    return props;
}

void SSAOPostProcess::setProperty(Property *prop)
{
    if(prop->name == "samples")
        samples = prop->getValue().toInt();
    else if(prop->name == "rings")
        rings = prop->getValue().toInt();
    else if(prop->name == "radius")
        radius = prop->getValue().toFloat();
    else if(prop->name == "diffArea")
        diffArea = prop->getValue().toFloat();
    else if(prop->name == "gaussBellCenter")
        gaussBellCenter = prop->getValue().toFloat();
    else if(prop->name == "lumInfluence")
        lumInfluence = prop->getValue().toFloat();
}

void SSAOPostProcess::process(PostProcessContext *ctx)
{
    shader->bind();
    ctx->sceneTexture->bind(0);
    ctx->depthTexture->bind(1);
    normals->bind(2);
    shader->setUniformValue("u_sceneTexture", 0);
    shader->setUniformValue("u_depthTexture", 1);
    shader->setUniformValue("u_randomNormal", 2);

    shader->setUniformValue("samples",samples);
    shader->setUniformValue("rings",rings);
    shader->setUniformValue("radius",radius);
    shader->setUniformValue("diffarea",diffArea);
    shader->setUniformValue("gdisplace",gaussBellCenter);
    shader->setUniformValue("lumInfluence",lumInfluence);

    ctx->manager->blit(Texture2D::null(), ctx->finalTexture, shader);
    shader->release();
}

SSAOPostProcessPtr SSAOPostProcess::create()
{
    return SSAOPostProcessPtr(new SSAOPostProcess());
}

}
