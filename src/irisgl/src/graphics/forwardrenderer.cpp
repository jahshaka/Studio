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
#include "../materials/viewermaterial.h"
#include "mesh.h"
#include "graphicshelper.h"
#include "renderdata.h"
#include "material.h"
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
#include "../core/irisutils.h"

#include <QOpenGLContext>
#include "../libovr/Include/OVR_CAPI_GL.h"
#include "../libovr/Include/Extras/OVR_Math.h"

using namespace OVR;

namespace iris
{

ForwardRenderer::ForwardRenderer(QOpenGLFunctions_3_2_Core* gl)
{
    this->gl = gl;
    renderData = new RenderData();

    billboard = new Billboard(gl);
    fsQuad = new FullScreenQuad();
    createLineShader();
    createShadowShader();
    createParticleShader();
    createEmitterShader();

    // particleSystems.push_back(new ParticleSystem(gl, QVector3D(0, 0, 0), 50, 25, .2f, 4));

    generateShadowBuffer(4096);

    vrDevice = new VrDevice(gl);
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

QSharedPointer<ForwardRenderer> ForwardRenderer::create(QOpenGLFunctions_3_2_Core* gl)
{
    return QSharedPointer<ForwardRenderer>(new ForwardRenderer(gl));
}

// all scene's transform should be updated
void ForwardRenderer::renderScene(QOpenGLContext* ctx, float delta, Viewport* vp)
{
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

    gl->glViewport(0, 0, 4096, 4096);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    gl->glClear(GL_DEPTH_BUFFER_BIT);
    gl->glCullFace(GL_FRONT);
    renderShadows(renderData, scene->rootNode);
    gl->glCullFace(GL_BACK);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());

    gl->glViewport(0, 0, vp->width, vp->height);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //enable all attrib arrays
    for(int i = 0;i < 8;i++)
        gl->glEnableVertexAttribArray(i);

    renderNode(renderData, scene->rootNode);

    // STEP 2: RENDER SKY
    renderSky(renderData);

    // STEP 3: RENDER LINES (for e.g. light radius and the camera frustum)
    renderParticles(renderData, delta, scene->rootNode);

    // STEP 4: RENDER BILLBOARD ICONS
    renderBillboardIcons(renderData);

    // STEP 5: RENDER SELECTED OBJECT
    if (!!selectedSceneNode) renderSelectedNode(renderData,selectedSceneNode);
}

void ForwardRenderer::renderShadows(RenderData* renderData,QSharedPointer<SceneNode> node)
{
    if (node->sceneNodeType == SceneNodeType::Mesh && node->isVisible()) {

        shadowShader->bind();

        QMatrix4x4 lightView, lightProjection;

        for (auto light : scene->lights) {
            if (light->lightType == iris::LightType::Directional && true) { // cast shadows
                auto meshNode = node.staticCast<MeshNode>();

                lightProjection.ortho(-128.0f, 128.0f, -64.0f, 64.0f, -64.0f, 128.0f);

                lightView.lookAt(QVector3D(0, 0, 0),
                                 light->getLightDir(),
                                 QVector3D(0.0f, 1.0f, 0.0f));

                QMatrix4x4 lightSpaceMatrix = lightProjection * lightView;


                gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadowFBO);
                gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthMap, 0);

                shadowShader->setUniformValue("u_worldMatrix", node->globalTransform);
                shadowShader->setUniformValue("u_lightSpaceMatrix", lightSpaceMatrix);

                if(meshNode->mesh != nullptr)
                    meshNode->mesh->draw(gl, shadowShader);
            }
        }

        shadowShader->release();
    }

    for (auto childNode : node->children) {
        renderShadows(renderData, childNode);
    }
}

void ForwardRenderer::renderParticles(RenderData *renderData, float delta, QSharedPointer<SceneNode> node)
{
    if (!particleSystems.empty()) {
        for (auto p : particleSystems) {
            p.second->generateParticles(delta);

            // this is pretty messy, find the meshnode with the same id as the emitter
            // and request info from it, also could an initializer list be user here...?
            for (auto n : node->children) {
                if (n->sceneNodeType == SceneNodeType::Emitter && n.staticCast<MeshNode>()->m_index == p.first) {
                    QList<SceneNodePtr>::iterator x = std::find(node->children.begin(), node->children.end(), n);
                    p.second->setPos((*x).staticCast<MeshNode>()->pos);
                    p.second->setPPS((*x).staticCast<MeshNode>()->pps);
                    p.second->setLife((*x).staticCast<MeshNode>()->particleLife);
                    p.second->setSpeed((*x).staticCast<MeshNode>()->speed);
                    p.second->setGravity((*x).staticCast<MeshNode>()->gravity);
                    p.second->setTexture((*x).staticCast<MeshNode>()->texture);
                    p.second->setDirection((*x).staticCast<MeshNode>()->getGlobalTransform());
                    p.second->setVolumeSquare((*x).staticCast<MeshNode>()->scale);
                    p.second->setSpeedError((*x).staticCast<MeshNode>()->speedFac);
                    p.second->setScaleError((*x).staticCast<MeshNode>()->scaleFac);
                    p.second->setLifeError((*x).staticCast<MeshNode>()->lifeFac);
                    p.second->dissipate((*x).staticCast<MeshNode>()->dissipate);
                    p.second->setRandomRotation((*x).staticCast<MeshNode>()->randomRotation);
                    p.second->setBlendMode((*x).staticCast<MeshNode>()->useAdditive);
                }
            }

            p.second->pm.update(delta);
            p.second->pm.renderParticles(gl, particleShader, renderData);
        }
    }
}

void ForwardRenderer::renderSceneVr(QOpenGLContext* ctx, float delta, Viewport* vp)
{
    if(!vrDevice->isVrSupported())
        return;

    //auto camera = scene->camera;

    QVector3D viewerPos = scene->camera->getGlobalPosition();
    float viewScale = scene->camera->getVrViewScale();

    if(!!scene->vrViewer) {
        viewerPos = scene->vrViewer->getGlobalPosition();
        viewScale = scene->vrViewer->getViewScale();
    }

    vrDevice->beginFrame();

    for (int eye = 0; eye < 2; ++eye)
    {
        vrDevice->beginEye(eye);

        auto view = vrDevice->getEyeViewMatrix(eye, viewerPos, viewScale);
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


        renderNode(renderData,scene->rootNode);

        //STEP 2: RENDER SKY
        renderSky(renderData);

        renderParticles(renderData, delta, scene->rootNode);


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
}

bool ForwardRenderer::isVrSupported()
{
    return vrDevice->isVrSupported();
}

void ForwardRenderer::renderNode(RenderData* renderData, QSharedPointer<SceneNode> node)
{
    iris::Mesh* mesh = nullptr;
    iris::Material* mat = nullptr;

    if(node->isVisible())
    {
        if(node->sceneNodeType == iris::SceneNodeType::Mesh)
        {
            auto meshNode = node.staticCast<MeshNode>();
            mesh = meshNode->mesh;
            mat = meshNode->material.data();
        }
        else if(node->sceneNodeType == iris::SceneNodeType::Viewer)
        {
            auto veiwerNode = node.staticCast<ViewerNode>();
            mesh = veiwerNode->headModel;
            mat = static_cast<iris::Material*>(veiwerNode->material.data());
        } else if (node->sceneNodeType == SceneNodeType::Emitter) {
            auto meshNode = node.staticCast<MeshNode>();

            // pass the id and create the system, hmm
            if (meshNode->isEmitter) {
                particleSystems.insert(
                            std::make_pair(
                                meshNode->m_index,
                                new ParticleSystem(gl,
                                                   meshNode->pos,
                                                   meshNode->pps,
                                                   meshNode->speed,
                                                   meshNode->gravity,
                                                   meshNode->particleLife,
                                                   1)
                            )
                );
            }

            emitterShader->bind();

            emitterShader->setUniformValue("projectionMatrix", renderData->projMatrix);
            emitterShader->setUniformValue("modelViewMatrix", renderData->viewMatrix * node->globalTransform);

            if (meshNode->mesh != nullptr) {
                gl->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                gl->glLineWidth(2);
                meshNode->mesh->draw(gl, emitterShader);
                gl->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
    }

    if(mesh != nullptr && mat != nullptr)
    {
        auto meshNode = node.staticCast<MeshNode>();
        //auto mat = meshNode->material;

        auto program = mat->program;

        mat->begin(gl,scene);

        //send transform and light data
        program->setUniformValue("u_worldMatrix",node->globalTransform);
        program->setUniformValue("u_viewMatrix",renderData->viewMatrix);
        program->setUniformValue("u_projMatrix",renderData->projMatrix);
        program->setUniformValue("u_normalMatrix",node->globalTransform.normalMatrix());

        program->setUniformValue("u_eyePos",renderData->eyePos);

        program->setUniformValue("u_fogData.color",renderData->fogColor);
        program->setUniformValue("u_fogData.start",renderData->fogStart);
        program->setUniformValue("u_fogData.end",renderData->fogEnd);
        program->setUniformValue("u_fogData.enabled",renderData->fogEnabled);

        //program->setUniformValue("u_textureScale",1.0f);

        program->setUniformValue("u_shadowMap", 2);

        gl->glActiveTexture(GL_TEXTURE2);
        gl->glBindTexture(GL_TEXTURE_2D, shadowDepthMap);

        QMatrix4x4 lightView, lightProjection;

        for (auto light : scene->lights) {
            if (light->lightType == iris::LightType::Directional && true) { // cast shadows
                lightProjection.ortho(-128.0f, 128.0f, -64.0f, 64.0f, -64.0f, 128.0f);

                lightView.lookAt(QVector3D(0, 0, 0),
                                 light->getLightDir(),
                                 QVector3D(0.0f, 1.0f, 0.0f));

                QMatrix4x4 lightSpaceMatrix = lightProjection * lightView;
                program->setUniformValue("u_lightSpaceMatrix", lightSpaceMatrix);
            }
        }

        auto lightCount = renderData->scene->lights.size();
        mat->program->setUniformValue("u_lightCount",lightCount);

        for(int i=0;i<lightCount;i++)
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

        if(meshNode->mesh != nullptr)
            meshNode->mesh->draw(gl, program);

    }

    for(auto childNode:node->children)
        renderNode(renderData,childNode);
}

void ForwardRenderer::renderSky(RenderData* renderData)
{
    if(scene->skyMesh == nullptr)
        return;

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
