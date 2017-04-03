/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "forwardrenderer.h"
#include "../core/scene.h"
#include "../core/scenenode.h"
#include "../scenegraph/cameranode.h"
#include "../scenegraph/meshnode.h"
#include "../scenegraph/lightnode.h"
#include "../scenegraph/viewernode.h"
#include "../scenegraph/particlesystemnode.h"
#include "../materials/viewermaterial.h"
#include "mesh.h"
#include "graphicshelper.h"
#include "renderdata.h"
#include "material.h"
#include "renderitem.h"
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QSharedPointer>
#include <QOpenGLTexture>
#include "viewport.h"
#include "utils/billboard.h"
#include "utils/fullscreenquad.h"
#include "texture2d.h"
#include "../vr/vrdevice.h"
#include "../vr/vrmanager.h"
#include "../core/irisutils.h"

#include <QOpenGLContext>
#include "../libovr/Include/OVR_CAPI_GL.h"
#include "../libovr/Include/Extras/OVR_Math.h"

using namespace OVR;

namespace iris
{

ForwardRenderer::ForwardRenderer()
{
    this->gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    renderData = new RenderData();

    billboard = new Billboard(gl);
    fsQuad = new FullScreenQuad();
    createLineShader();
    createShadowShader();
    createParticleShader();
    createEmitterShader();

    generateShadowBuffer(4096);

    vrDevice = VrManager::getDefaultDevice();
    vrDevice->initialize();
}

void ForwardRenderer::generateShadowBuffer(GLuint size)
{
    gl->glGenFramebuffers(1, &shadowFBO);

    gl->glGenTextures(1, &shadowDepthMap);
    gl->glBindTexture(GL_TEXTURE_2D, shadowDepthMap);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32,
                     size, size,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthMap, 0);

    gl->glDrawBuffer(GL_NONE);
    gl->glReadBuffer(GL_NONE);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // check status at end
}

ForwardRendererPtr ForwardRenderer::create()
{
    return ForwardRendererPtr(new ForwardRenderer());
}

// all scene's transform should be updated
void ForwardRenderer::renderScene(float delta, Viewport* vp)
{
    auto ctx = QOpenGLContext::currentContext();
    auto cam = scene->camera;

    // STEP 1: RENDER SCENE
    renderData->scene = scene;

    cam->setAspectRatio(vp->getAspectRatio());
    cam->updateCameraMatrices();

    renderData->projMatrix = cam->projMatrix;
    renderData->viewMatrix = cam->viewMatrix;
    renderData->eyePos = cam->globalTransform.column(3).toVector3D();

    renderData->fogColor = scene->fogColor;
    renderData->fogStart = scene->fogStart;
    renderData->fogEnd = scene->fogEnd;
    renderData->fogEnabled = scene->fogEnabled;

    if (scene->shadowEnabled) {
        gl->glViewport(0, 0, 4096, 4096);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        gl->glClear(GL_DEPTH_BUFFER_BIT);
        gl->glCullFace(GL_FRONT);
        renderShadows(scene);
        gl->glCullFace(GL_BACK);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());
    }

    gl->glViewport(0, 0, vp->width * vp->pixelRatioScale, vp->height * vp->pixelRatioScale);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //enable all attrib arrays
    for (int i = 0; i < 8; i++) {
        gl->glEnableVertexAttribArray(i);
    }

    renderNode(renderData, scene);

    // STEP 2: RENDER SKY
    //renderSky(renderData);

    // STEP 4: RENDER BILLBOARD ICONS
    renderBillboardIcons(renderData);

    // STEP 5: RENDER SELECTED OBJECT
    if (!!selectedSceneNode) renderSelectedNode(renderData,selectedSceneNode);

    //clear lists
    scene->geometryRenderList.clear();
    scene->shadowRenderList.clear();
}

void ForwardRenderer::renderShadows(QSharedPointer<Scene> node)
{
    QMatrix4x4 lightView, lightProjection;

    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadowFBO);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthMap, 0);

    shadowShader->bind();

    for (auto light : scene->lights) {
        if (light->lightType == iris::LightType::Directional) {

            lightProjection.ortho(-128.0f, 128.0f, -64.0f, 64.0f, -64.0f, 128.0f);

            lightView.lookAt(QVector3D(0, 0, 0),
                             light->getLightDir(),
                             QVector3D(0.0f, 1.0f, 0.0f));
            QMatrix4x4 lightSpaceMatrix = lightProjection * lightView;

            shadowShader->setUniformValue("u_lightSpaceMatrix", lightSpaceMatrix);

            for (auto& item : scene->shadowRenderList) {
                shadowShader->setUniformValue("u_worldMatrix", item->worldMatrix);

                if (item->type == iris::RenderItemType::Mesh) {
                    item->mesh->draw(gl, shadowShader);
                }
            }
        }
    }

    shadowShader->release();
}

void ForwardRenderer::renderSceneVr(float delta, Viewport* vp)
{
    auto ctx = QOpenGLContext::currentContext();
    if(!vrDevice->isVrSupported())
        return;

    QVector3D viewerPos = scene->camera->getGlobalPosition();
    float viewScale = scene->camera->getVrViewScale();
    QMatrix4x4 viewTransform = scene->camera->globalTransform;
    //viewTransform.setToIdentity();

    /*
    if(!!scene->vrViewer) {
        viewerPos = scene->vrViewer->getGlobalPosition();
        viewScale = scene->vrViewer->getViewScale();
        viewTransform = scene->vrViewer->globalTransform;
    }
    */

    if (scene->shadowEnabled) {
        gl->glViewport(0, 0, 4096, 4096);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        gl->glClear(GL_DEPTH_BUFFER_BIT);
        gl->glCullFace(GL_FRONT);
        renderShadows(scene);
        gl->glCullFace(GL_BACK);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());
    }

    vrDevice->beginFrame();

    for (int eye = 0; eye < 2; ++eye)
    {
        vrDevice->beginEye(eye);

        auto view = vrDevice->getEyeViewMatrix(eye, viewerPos, viewTransform);
        renderData->eyePos = view.column(3).toVector3D();
        renderData->viewMatrix = view;

        auto proj = vrDevice->getEyeProjMatrix(eye,0.1f,1000.0f);
        renderData->projMatrix = proj;

        //STEP 1: RENDER SCENE
        renderData->scene = scene;

        //camera->setAspectRatio(vp->getAspectRatio());
        //camera->updateCameraMatrices();

        //renderData->eyePos = camera->globalTransform.column(3).toVector3D();
        renderData->eyePos = viewerPos;

        renderData->fogColor = scene->fogColor;
        renderData->fogStart = scene->fogStart;
        renderData->fogEnd = scene->fogEnd;
        renderData->fogEnabled = scene->fogEnabled;

        renderNode(renderData,scene);

        //STEP 2: RENDER SKY
        //renderSky(renderData);

        //renderParticles(renderData, delta, scene->rootNode);


        vrDevice->endEye(eye);
    }

    vrDevice->endFrame();

   //rendering to the window
   gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());

   gl->glViewport(0, 0, vp->width,vp->height);
   gl->glActiveTexture(GL_TEXTURE0);
   vrDevice->bindMirrorTextureId();
   fsQuad->draw(gl);
   gl->glBindTexture(GL_TEXTURE_2D,0);

   scene->geometryRenderList.clear();
   scene->shadowRenderList.clear();
}

bool ForwardRenderer::isVrSupported()
{
    return vrDevice->isVrSupported();
}

void ForwardRenderer::renderNode(RenderData* renderData, ScenePtr scene)
{
    QMatrix4x4 lightView, lightProjection, lightSpaceMatrix;

    for (auto light : scene->lights) {
        if (light->lightType == iris::LightType::Directional && true) { // cast shadows
            lightProjection.ortho(-128.0f, 128.0f, -64.0f, 64.0f, -64.0f, 128.0f);

            lightView.lookAt(QVector3D(0, 0, 0),
                             light->getLightDir(),
                             QVector3D(0.0f, 1.0f, 0.0f));

            lightSpaceMatrix = lightProjection * lightView;
        }
    }

    auto lightCount = renderData->scene->lights.size();

    //sort render list
    //qsort(scene->geometryRenderList,scene->geometryRenderList.size(),)
    qSort(scene->geometryRenderList.begin(), scene->geometryRenderList.end(), [](const RenderItem* a, const RenderItem* b) {
        return a->renderLayer < b->renderLayer;
    });

    for (auto& item : scene->geometryRenderList) {
        if (item->type == iris::RenderItemType::Mesh) {

            QOpenGLShaderProgram* program = nullptr;
            iris::MaterialPtr mat;

            // if a material is set then use it and gets its shaderprogram

            if (!!item->material) {
                mat = item->material;
                program = mat->program;

                mat->begin(gl, scene);
            } else {
                program = item->shaderProgram;
            }

            // send transform and light data
            program->setUniformValue("u_worldMatrix",   item->worldMatrix);
            program->setUniformValue("u_viewMatrix",    renderData->viewMatrix);
            program->setUniformValue("u_projMatrix",    renderData->projMatrix);
            program->setUniformValue("u_normalMatrix",  item->worldMatrix.normalMatrix());

            program->setUniformValue("u_eyePos",        renderData->eyePos);

            if (item->renderStates.fogEnabled && scene->fogEnabled ) {
                program->setUniformValue("u_fogData.color", renderData->fogColor);
                program->setUniformValue("u_fogData.start", renderData->fogStart);
                program->setUniformValue("u_fogData.end",   renderData->fogEnd);

                program->setUniformValue("u_fogData.enabled", true);
            } else {
                program->setUniformValue("u_fogData.enabled", false);
            }

            if (item->renderStates.receiveShadows && scene->shadowEnabled) {
                program->setUniformValue("u_shadowMap", 2);
                program->setUniformValue("u_shadowEnabled", true);
            } else {
                program->setUniformValue("u_shadowEnabled", false);
            }

            gl->glActiveTexture(GL_TEXTURE2);
            gl->glBindTexture(GL_TEXTURE_2D, shadowDepthMap);

            program->setUniformValue("u_lightSpaceMatrix",  lightSpaceMatrix);
            program->setUniformValue("u_lightCount",        lightCount);

            // only materials get lights passed to it
            if ( item->renderStates.receiveLighting ) {
                for (int i=0;i<lightCount;i++)
                {
                    QString lightPrefix = QString("u_lights[%0].").arg(i);

                    auto light = renderData->scene->lights[i];
                    if(!light->isVisible())
                    {
                        //quick hack for now
                        mat->setUniformValue(lightPrefix+"color", QColor(0,0,0));
                        continue;
                    }

                    mat->setUniformValue(lightPrefix+"type", (int)light->lightType);
                    mat->setUniformValue(lightPrefix+"position", light->globalTransform.column(3).toVector3D());
                    //mat->setUniformValue(lightPrefix+"direction", light->getDirection());
                    mat->setUniformValue(lightPrefix+"distance", light->distance);
                    mat->setUniformValue(lightPrefix+"direction", light->getLightDir());
                    mat->setUniformValue(lightPrefix+"cutOffAngle", light->spotCutOff);
                    mat->setUniformValue(lightPrefix+"cutOffSoftness", light->spotCutOffSoftness);
                    mat->setUniformValue(lightPrefix+"intensity", light->intensity);
                    mat->setUniformValue(lightPrefix+"color", light->color);

                    mat->setUniformValue(lightPrefix+"constantAtten", 1.0f);
                    mat->setUniformValue(lightPrefix+"linearAtten", 0.0f);
                    mat->setUniformValue(lightPrefix+"quadtraticAtten", 1.0f);
                }
            }

            // set culling state
            // FaceCullingMode::Back is the default state
            if (item->renderStates.cullMode != FaceCullingMode::Back) {
               switch(item->renderStates.cullMode) {
                case FaceCullingMode::Front:
                    gl->glCullFace(GL_FRONT);
                break;
               case FaceCullingMode::FrontAndBack:
                   gl->glCullFace(GL_FRONT_AND_BACK);
               break;
               case FaceCullingMode::None:
                   gl->glDisable(GL_CULL_FACE);
               break;
               }
            }

            if (!item->renderStates.zWrite) {
                gl->glDepthMask(false);
            }

            if (!item->renderStates.depthTest) {
                gl->glDisable(GL_DEPTH_TEST);
            }

            item->mesh->draw(gl, program);

            if (!!mat) {
                mat->end(gl,scene);
            }

            // change back culling state
            if (item->renderStates.cullMode != FaceCullingMode::Back) {
                gl->glCullFace(GL_BACK);
            } else if(item->renderStates.cullMode != FaceCullingMode::None) {
                gl->glEnable(GL_CULL_FACE);
            }

            if (!item->renderStates.zWrite) {
                gl->glDepthMask(true);
            }

            if (!item->renderStates.depthTest) {
                gl->glEnable(GL_DEPTH_TEST);
            }
        }
        else if(item->type == iris::RenderItemType::ParticleSystem) {

            auto ps = item->sceneNode.staticCast<ParticleSystemNode>();
            ps->renderParticles(renderData, particleShader);
        }
    }

}

void ForwardRenderer::renderSky(RenderData* renderData)
{
    if (scene->skyMesh == nullptr) return;

    gl->glDepthMask(false);

    scene->skyMaterial->begin(gl,scene);

    auto program = scene->skyMaterial->program;
    program->setUniformValue("u_viewMatrix",renderData->viewMatrix);
    program->setUniformValue("u_projMatrix",renderData->projMatrix);
    QMatrix4x4 worldMatrix;
    worldMatrix.setToIdentity();
    program->setUniformValue("u_worldMatrix",worldMatrix);

    scene->skyMesh->draw(gl,program);
    scene->skyMaterial->end(gl,scene);

    gl->glDepthMask(true);
}

void ForwardRenderer::renderBillboardIcons(RenderData* renderData)
{
    gl->glDisable(GL_CULL_FACE);

    auto lightCount = renderData->scene->lights.size();
    auto program = billboard->program;
    program->bind();

    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for(int i=0;i<lightCount;i++)
    {
        auto light = renderData->scene->lights[i];

        program->setUniformValue("u_worldMatrix",light->globalTransform);
        program->setUniformValue("u_viewMatrix",renderData->viewMatrix);
        program->setUniformValue("u_projMatrix",renderData->projMatrix);

        gl->glActiveTexture(GL_TEXTURE0);
        auto icon = light->icon;
        if(!!icon)
        {
            icon->texture->bind();
        }
        else
        {
            gl->glBindTexture(GL_TEXTURE_2D,0);
        }

        billboard->draw(gl);
    }
    gl->glDisable(GL_BLEND);

    gl->glEnable(GL_CULL_FACE);
}

// http://gamedev.stackexchange.com/questions/59361/opengl-get-the-outline-of-multiple-overlapping-objects
void ForwardRenderer::renderSelectedNode(RenderData* renderData,QSharedPointer<SceneNode> node)
{
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();

        if (meshNode->mesh != nullptr) {
            lineShader->bind();

            lineShader->setUniformValue("u_worldMatrix",    node->globalTransform);
            lineShader->setUniformValue("u_viewMatrix",     renderData->viewMatrix);
            lineShader->setUniformValue("u_projMatrix",     renderData->projMatrix);
            lineShader->setUniformValue("u_normalMatrix",   node->globalTransform.normalMatrix());
            lineShader->setUniformValue("color",            scene->outlineColor);


            // STEP 1: DRAW STENCIL OF THE FILLED POLYGON
            // sets default stencil value to 0
            gl->glClearStencil(0);
            gl->glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            // this should be the default
            gl->glPolygonMode(GL_FRONT, GL_FILL);

            gl->glEnable(GL_STENCIL_TEST);

            // works the same as the color and depth mask
            gl->glStencilMask(1);
            // test must always pass
            gl->glStencilFunc(GL_ALWAYS, 1, OUTLINE_STENCIL_CHANNEL);
            // GL_REPLACE for all becase a stencil value should always be written
            gl->glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            // disable drawing to color buffer
            gl->glColorMask(0, 0 ,0, 0);

            meshNode->mesh->draw(gl,lineShader);

            // STEP 2: DRAW MESH IN LINE MODE WITH A LINE WIDTH > 1 SO THE OUTLINE PASSES THE STENCIL TEST
            gl->glPolygonMode(GL_FRONT, GL_LINE);
            gl->glLineWidth(scene->outlineWidth);

            /* the default stencil value is 0, if the stencil value at a pixel is 1 that means
             * thats where the solid mesh was rendered. The line version should only be rendered
             * where the stencil value is 0.
             */
            gl->glStencilFunc(GL_EQUAL, 0, OUTLINE_STENCIL_CHANNEL);
            gl->glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            // disables writing to stencil buffer
            gl->glStencilMask(0);
            // enable drawing to color buffer
            gl->glColorMask(1, 1, 1, 1);

            meshNode->mesh->draw(gl, lineShader);

            gl->glDisable(GL_STENCIL_TEST);

            gl->glStencilMask(1);
            gl->glLineWidth(1);
            gl->glPolygonMode(GL_FRONT, GL_FILL);
        }
    }
}

void ForwardRenderer::createLineShader()
{
    lineShader = GraphicsHelper::loadShader(":assets/shaders/color.vert",
                                              ":assets/shaders/color.frag");

    lineShader->bind();
    lineShader->setUniformValue("color",QColor(240,240,255,255));
}

void ForwardRenderer::createShadowShader()
{
    shadowShader = GraphicsHelper::loadShader(":assets/shaders/shadow_map.vert",
                                              ":assets/shaders/shadow_map.frag");

    shadowShader->bind();
}

void ForwardRenderer::createParticleShader()
{
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile(":app/shaders/particle.vert");

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile(":app/shaders/particle.frag");

    particleShader = new QOpenGLShaderProgram;
    particleShader->addShader(vshader);
    particleShader->addShader(fshader);

    particleShader->link();

    particleShader->bind();
}

void ForwardRenderer::createEmitterShader()
{
    emitterShader = GraphicsHelper::loadShader(":app/shaders/emitter.vert",
                                               ":app/shaders/emitter.frag");
}

ForwardRenderer::~ForwardRenderer()
{
    delete vrDevice;
}

}
