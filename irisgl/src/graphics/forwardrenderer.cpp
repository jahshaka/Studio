/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "forwardrenderer.h"
#include "../scenegraph/scene.h"
#include "../scenegraph/scenenode.h"
#include "../scenegraph/cameranode.h"
#include "../scenegraph/meshnode.h"
#include "../scenegraph/lightnode.h"
#include "../scenegraph/viewernode.h"
#include "../scenegraph/particlesystemnode.h"
#include "mesh.h"
#include "skeleton.h"
#include "graphicshelper.h"
#include "renderdata.h"
#include "shader.h"
#include "material.h"
#include "renderitem.h"
#include "shadowmap.h"
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QSharedPointer>
#include <QOpenGLTexture>
#include <QMatrix4x4>
#include "viewport.h"
#include "utils/billboard.h"
#include "utils/fullscreenquad.h"
#include "texture2d.h"
#include "rendertarget.h"
#include "renderlist.h"
#include "graphicsdevice.h"
#include "spritebatch.h"
#include "font.h"
#include "../vr/vrdevice.h"
#include "../vr/vrmanager.h"
#include "../core/irisutils.h"
#include "postprocessmanager.h"
#include "postprocess.h"

#include "../core/performancetimer.h"

#include "../geometry/frustum.h"
#include "../geometry/boundingsphere.h"

#include "utils/shapehelper.h"
#include "../materials/colormaterial.h"

#include <QOpenGLContext>
#include "../libovr/Include/OVR_CAPI_GL.h"
#include "../libovr/Include/Extras/OVR_Math.h"


using namespace OVR;

namespace iris
{

ForwardRenderer::ForwardRenderer(bool supportsVr, bool physicsEnabled)
{
    this->gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    graphics = GraphicsDevice::create();

    renderData = new RenderData();

    billboard = new Billboard();
    fsQuad = new FullScreenQuad();
    createLineShader();
    createShadowShader();
    createParticleShader();
    createEmitterShader();

    generateShadowBuffer(4096);

    if (supportsVr) {
        vrDevice = VrManager::getDefaultDevice();
        vrDevice->initialize();
	} else {
		vrDevice = Q_NULLPTR;
	}

    renderTarget = RenderTarget::create(800, 800);
    sceneRenderTexture = Texture2D::create(800, 800);
    depthRenderTexture = Texture2D::createDepth(800, 800);
    finalRenderTexture = Texture2D::create(800, 800);
    renderTarget->addTexture(sceneRenderTexture);
    renderTarget->setDepthTexture(depthRenderTexture);

    postMan = PostProcessManager::create(graphics);
    postContext = new PostProcessContext();

    perfTimer = new PerformanceTimer();

    renderLightBillboards = true;
	generateLightUnformNames();

}

void ForwardRenderer::generateShadowBuffer(GLuint size)
{
    gl->glGenFramebuffers(1, &shadowFBO);
/*
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
*/
    gl->glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    //gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthMap, 0);

    gl->glDrawBuffer(GL_NONE);
    gl->glReadBuffer(GL_NONE);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // check status at end
}

ForwardRendererPtr ForwardRenderer::create(bool useVr, bool physicsEnabled)
{
    return ForwardRendererPtr(new ForwardRenderer(useVr, physicsEnabled));
}

GraphicsDevicePtr ForwardRenderer::getGraphicsDevice()
{
    return graphics;
}

void ForwardRenderer::renderSceneToRenderTarget(RenderTargetPtr rt, CameraNodePtr cam, bool clearRenderLists, bool applyPostProcesses)
{
    auto ctx = QOpenGLContext::currentContext();

    // reset states
    graphics->setBlendState(BlendState::Opaque, true);
    graphics->setDepthState(DepthState::Default, true);
    graphics->setRasterizerState(RasterizerState::CullCounterClockwise, true);

    // STEP 1: RENDER SCENE
    renderData->scene = scene;

    cam->setAspectRatio(rt->getWidth()/(float)rt->getHeight());
    cam->updateCameraMatrices();

    renderData->projMatrix = cam->projMatrix;
    renderData->viewMatrix = cam->viewMatrix;
    renderData->eyePos = cam->globalTransform.column(3).toVector3D();

    renderData->frustum.build(cam->projMatrix * cam->viewMatrix);

    renderData->fogColor = scene->fogColor;
    renderData->fogStart = scene->fogStart;
    renderData->fogEnd = scene->fogEnd;
    renderData->fogEnabled = scene->fogEnabled;

    if (scene->shadowEnabled) {
        renderShadows(scene);
    }

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //todo: remember to remove this!
    renderTarget->resize(rt->getWidth(), rt->getHeight(), true);
    finalRenderTexture->resize(rt->getWidth(), rt->getHeight());

    renderTarget->bind();
	graphics->setViewport(QRect(0, 0, rt->getWidth(), rt->getHeight()));
	graphics->clear(QColor(0, 0, 0, 0));

    // reset states
    graphics->setBlendState(BlendState::Opaque, true);
    graphics->setDepthState(DepthState::Default, true);
    graphics->setRasterizerState(RasterizerState::CullCounterClockwise, true);

    renderNode(renderData, scene);

    if (renderLightBillboards)
        renderBillboardIcons(renderData);

    renderTarget->unbind();

	// reset states for post processing
	graphics->setBlendState(BlendState::Opaque, true);
	graphics->setDepthState(DepthState::Default, true);
	graphics->setRasterizerState(RasterizerState::CullCounterClockwise, true);

    if (applyPostProcesses) {
        postContext->sceneTexture = sceneRenderTexture;
        postContext->depthTexture = depthRenderTexture;
        postContext->finalTexture = finalRenderTexture;
        postMan->process(postContext);
    }


    // draw fs quad
    rt->bind();
    gl->glViewport(0, 0, rt->getWidth(), rt->getHeight());
    gl->glClearColor(0, 0, 0, 0);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl->glActiveTexture(GL_TEXTURE0);

    graphics->setBlendState(BlendState::Opaque, true);
    graphics->setDepthState(DepthState::Default, true);
    graphics->setRasterizerState(RasterizerState::CullNone, true);

	//applyPostProcesses = false;
    if (applyPostProcesses)
        postContext->finalTexture->bind();
    else
        sceneRenderTexture->bind();
        
    fsQuad->draw(graphics);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    rt->unbind();

    //clear lists
    if (clearRenderLists) {
        scene->geometryRenderList->clear();
        scene->shadowRenderList->clear();
        scene->gizmoRenderList->clear();
    }
}

void ForwardRenderer::renderScene(float delta, Viewport* vp)
{
    //perfTimer->start("total");
    auto ctx = QOpenGLContext::currentContext();
    auto cam = scene->camera;

    // reset states
    graphics->setBlendState(BlendState::Opaque, true);
    graphics->setDepthState(DepthState::Default, true);
    graphics->setRasterizerState(RasterizerState::CullCounterClockwise, true);

    // STEP 1: RENDER SCENE
    //perfTimer->start("render_scene");
    renderData->scene = scene;

    cam->setAspectRatio(vp->getAspectRatio());
    cam->updateCameraMatrices();

    renderData->projMatrix = cam->projMatrix;
    renderData->viewMatrix = cam->viewMatrix;
    renderData->eyePos = cam->globalTransform.column(3).toVector3D();

    renderData->frustum.build(cam->projMatrix * cam->viewMatrix);

    renderData->fogColor = scene->fogColor;
    renderData->fogStart = scene->fogStart;
    renderData->fogEnd = scene->fogEnd;
    renderData->fogEnabled = scene->fogEnabled;

    if (scene->shadowEnabled) {
        renderShadows(scene);
    }

    gl->glViewport(0, 0, vp->width * vp->pixelRatioScale, vp->height * vp->pixelRatioScale);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    renderTarget->resize(vp->width * vp->pixelRatioScale, vp->height * vp->pixelRatioScale, true);
    finalRenderTexture->resize(vp->width * vp->pixelRatioScale, vp->height * vp->pixelRatioScale);

    renderTarget->bind();
    graphics->setViewport(QRect(0, 0, vp->width * vp->pixelRatioScale, vp->height * vp->pixelRatioScale));
    graphics->clear(QColor(0, 0, 0, 0));

    // reset states
    graphics->setBlendState(BlendState::Opaque, true);
    graphics->setDepthState(DepthState::Default, true);
    graphics->setRasterizerState(RasterizerState::CullCounterClockwise, true);

    renderNode(renderData, scene);

    //perfTimer->end("render_scene");
    if (renderLightBillboards)
        renderBillboardIcons(renderData);
    //perfTimer->end("render_icons");

    //perfTimer->start("render_post");
    renderTarget->unbind();

	// reset these states for post processing
	graphics->setBlendState(BlendState::Opaque, true);
	graphics->setDepthState(DepthState::Default, true);
	graphics->setRasterizerState(RasterizerState::CullCounterClockwise, true);

    postContext->sceneTexture = sceneRenderTexture;
    postContext->depthTexture = depthRenderTexture;
    postContext->finalTexture = finalRenderTexture;
    postMan->process(postContext);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());

    // draw fs quad
    graphics->setViewport(QRect(0, 0, vp->width * vp->pixelRatioScale, vp->height * vp->pixelRatioScale));
    graphics->clear(QColor(0,0,0));

    graphics->setBlendState(BlendState::Opaque, true);
    graphics->setDepthState(DepthState::Default, true);
    graphics->setRasterizerState(RasterizerState::CullNone, true);

    gl->glActiveTexture(GL_TEXTURE0);
    postContext->finalTexture->bind();
    fsQuad->draw(graphics);
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    graphics->clear(GL_DEPTH_BUFFER_BIT);
    // STEP 5: RENDER SELECTED OBJECT
    //if (!!selectedSceneNode && selectedSceneNode->isVisible())
	//	renderSelectedNode(renderData,selectedSceneNode);

    //clear lists
    scene->geometryRenderList->clear();
    scene->shadowRenderList->clear();
    scene->gizmoRenderList->clear();

    //perfTimer->end("total");

    //perfTimer->report();
    //perfTimer->reset();
}

void ForwardRenderer::renderShadows(ScenePtr node)
{
    for (auto light : scene->lights) {
		if (light->getShadowMapType() != iris::ShadowMapType::None) {
			if (light->lightType == iris::LightType::Directional) {
				renderDirectionalShadow(light, scene);
			}
			else if (light->lightType == iris::LightType::Spot) {
				renderSpotlightShadow(light, scene);
			}
		}
    }
}

void ForwardRenderer::renderDirectionalShadow(LightNodePtr light, ScenePtr node)
{
    graphics->setRenderTarget(QList<Texture2DPtr>(),light->shadowMap->shadowTexture);
	graphics->setRasterizerState(RasterizerState::CullClockwise);

    int shadowSize = light->shadowMap->resolution;
    graphics->setViewport(QRect(0, 0, shadowSize, shadowSize));
    graphics->clear(QColor());
    //gl->glClear(COLOR_BUFFER_BIT|DEPTH_BUFFER_BIT);
    QMatrix4x4 lightProjection, lightView;
    QOpenGLShaderProgram* shader;

    lightProjection.ortho(-128.0f, 128.0f, -64.0f, 64.0f, -64.0f, 128.0f);

    lightView.lookAt(QVector3D(0, 0, 0),
                     light->getLightDir(),
                     QVector3D(0.0001f, 1.0001f, 0.0001f));
    QMatrix4x4 lightSpaceMatrix = lightProjection * lightView;
    light->shadowMap->shadowMatrix = lightSpaceMatrix;

    for (auto& item : scene->shadowRenderList->getItems()) {


        if (item->type == iris::RenderItemType::Mesh && !!item->mesh) {
            if  (item->mesh->hasSkeleton()) {
                auto boneTransforms = item->mesh->getSkeleton()->boneTransforms;
                skinnedShadowShader->bind();
                skinnedShadowShader->setUniformValue("u_lightSpaceMatrix", lightSpaceMatrix);
                skinnedShadowShader->setUniformValue("u_worldMatrix", item->worldMatrix);
                skinnedShadowShader->setUniformValueArray("u_bones", boneTransforms.data(), boneTransforms.size());
                shader = skinnedShadowShader;

            } else {
                shadowShader->bind();
                shadowShader->setUniformValue("u_lightSpaceMatrix", lightSpaceMatrix);
                shadowShader->setUniformValue("u_worldMatrix", item->worldMatrix);
                shader = shadowShader;
            }



            //item->mesh->draw(gl, shader);
            item->mesh->draw(graphics);
        }
    }
	graphics->setRasterizerState(RasterizerState::CullCounterClockwise);
    graphics->clearRenderTarget();
}

void ForwardRenderer::renderSpotlightShadow(LightNodePtr light, ScenePtr node)
{
    graphics->setRenderTarget(QList<Texture2DPtr>(),light->shadowMap->shadowTexture);
	graphics->setRasterizerState(RasterizerState::CullClockwise);
    //gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadowFBO);
    //gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, light->shadowMap->shadowTexture->getTextureId(), 0);
    //gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, light->shadowMap->shadowTexId, 0);
    //gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthMap, 0);

    int shadowSize = light->shadowMap->resolution;
    graphics->setViewport(QRect(0, 0, shadowSize, shadowSize));
    graphics->clear(QColor());

    QMatrix4x4 lightProjection, lightView;
    QOpenGLShaderProgram* shader;

    lightProjection.perspective(light->spotCutOff*2, 1,0.1f,light->distance);

    lightView.lookAt(light->getGlobalPosition(),
                     light->getGlobalPosition() + light->getLightDir() + QVector3D(0.0001,0.0001,0.0001),
                     QVector3D(0.0f, 1.0f, 0.0f));
    QMatrix4x4 lightSpaceMatrix = lightProjection * lightView;
    light->shadowMap->shadowMatrix = lightSpaceMatrix;

    for (auto& item : scene->shadowRenderList->getItems()) {


        if (item->type == iris::RenderItemType::Mesh && !!item->mesh) {
            if  (item->mesh->hasSkeleton()) {
                auto boneTransforms = item->mesh->getSkeleton()->boneTransforms;
                skinnedShadowShader->bind();
                skinnedShadowShader->setUniformValue("u_lightSpaceMatrix", lightSpaceMatrix);
                skinnedShadowShader->setUniformValue("u_worldMatrix", item->worldMatrix);
                skinnedShadowShader->setUniformValueArray("u_bones", boneTransforms.data(), boneTransforms.size());
                shader = skinnedShadowShader;

            } else {
                shadowShader->bind();
                shadowShader->setUniformValue("u_lightSpaceMatrix", lightSpaceMatrix);
                shadowShader->setUniformValue("u_worldMatrix", item->worldMatrix);
                shader = shadowShader;
            }

            //item->mesh->draw(gl, shader);
            item->mesh->draw(graphics);
        }
    }

	graphics->setRasterizerState(RasterizerState::CullCounterClockwise);
    graphics->clearRenderTarget();
}

void ForwardRenderer::renderSceneVr(float delta, Viewport* vp, bool useViewer)
{
    auto ctx = QOpenGLContext::currentContext();
    if(!vrDevice->isVrSupported())
        return;

    QVector3D viewerPos = scene->camera->getGlobalPosition();
    QMatrix4x4 viewTransform = scene->camera->globalTransform;

	/*
    if(!!scene->vrViewer && useViewer) {
        viewerPos = scene->vrViewer->getGlobalPosition();
        viewTransform = scene->vrViewer->globalTransform;
    }
	*/

    // reset states
    graphics->setBlendState(BlendState::Opaque);
    graphics->setDepthState(DepthState::Default);
    graphics->setRasterizerState(RasterizerState::CullCounterClockwise);

    if (scene->shadowEnabled) {
        renderShadows(scene);
    }

    vrDevice->beginFrame();

    for (int eye = 0; eye < 2; ++eye)
    {
		// states need to be reset before the framebuffer it set and cleared
		// glClear adheres to states that prevent writing to certain buffers
		// like glDepthMask or glColorMask
		graphics->setBlendState(BlendState::Opaque);
		graphics->setDepthState(DepthState::Default);
		graphics->setRasterizerState(RasterizerState::CullCounterClockwise);

        vrDevice->beginEye(eye);

        auto view = vrDevice->getEyeViewMatrix(eye, viewerPos, viewTransform);
        renderData->eyePos = view.column(3).toVector3D();
        renderData->viewMatrix = view;

        auto proj = vrDevice->getEyeProjMatrix(eye,0.1f,1000.0f);
        renderData->projMatrix = proj;

        //STEP 1: RENDER SCENE
        renderData->scene = scene;
        renderData->eyePos = viewerPos;

        renderData->fogColor = scene->fogColor;
        renderData->fogStart = scene->fogStart;
        renderData->fogEnd = scene->fogEnd;
        renderData->fogEnabled = scene->fogEnabled;

        // reset states
        graphics->setBlendState(BlendState::Opaque);
        graphics->setDepthState(DepthState::Default);
        graphics->setRasterizerState(RasterizerState::CullCounterClockwise);

        renderNode(renderData,scene);

		//if (renderLightBillboards)
		//	renderBillboardIcons(renderData);

        vrDevice->endEye(eye);
    }

    vrDevice->endFrame();

   //rendering to the window
   gl->glBindFramebuffer(GL_FRAMEBUFFER, ctx->defaultFramebufferObject());

   gl->glViewport(0, 0, vp->width * vp->pixelRatioScale,vp->height * vp->pixelRatioScale);
   gl->glActiveTexture(GL_TEXTURE0);

   graphics->setBlendState(BlendState::Opaque);
   graphics->setDepthState(DepthState::Default);
   graphics->setRasterizerState(RasterizerState::CullNone);

   vrDevice->bindMirrorTextureId();
   //vrDevice->bindEyeTexture(0);
   fsQuad->draw(graphics, true);
   gl->glBindTexture(GL_TEXTURE_2D,0);

   scene->geometryRenderList->clear();
   scene->shadowRenderList->clear();
   scene->gizmoRenderList->clear();
}

PostProcessManagerPtr ForwardRenderer::getPostProcessManager()
{
    return postMan;
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

    scene->geometryRenderList->sort();

    for (auto& item : scene->geometryRenderList->getItems()) {
        if (item->type == iris::RenderItemType::Mesh && !!item->mesh) {
            if (item->cullable) {
                auto sphere = item->boundingSphere;
                if (!renderData->frustum.isSphereInside(&sphere)) continue;
            }

            QOpenGLShaderProgram* program = nullptr;
            iris::MaterialPtr mat;

            // if a material is set then use it and get its shaderprogram

            if (!!item->material) {
                mat = item->material;
                //program = mat->getProgram();

                mat->begin(graphics, scene);
				graphics->setShader(mat->shader);
            } else {
                program = item->shaderProgram;
                program->bind();
            }

            // send transform and light data
			graphics->setShaderUniform("u_worldMatrix",   item->worldMatrix);
			graphics->setShaderUniform("u_viewMatrix",    renderData->viewMatrix);
			graphics->setShaderUniform("u_projMatrix",    renderData->projMatrix);

			graphics->setShaderUniform("u_time", scene->getRunningTime());

            if  (item->mesh->hasSkeleton()) {
                auto& boneTransforms = item->mesh->getSkeleton()->boneTransforms;
                graphics->setShaderUniformArray("u_bones", boneTransforms.data(), boneTransforms.size());
				
			}

			graphics->setShaderUniform("u_normalMatrix",  item->worldMatrix.normalMatrix());

			graphics->setShaderUniform("u_eyePos",        renderData->eyePos);
			graphics->setShaderUniform("u_sceneAmbient",  QVector3D(scene->ambientColor.redF(),
                                                                  scene->ambientColor.greenF(),
                                                                  scene->ambientColor.blueF()));

            if (item->renderStates.fogEnabled && scene->fogEnabled ) {
				graphics->setShaderUniform("u_fogData.color", renderData->fogColor);
				graphics->setShaderUniform("u_fogData.start", renderData->fogStart);
				graphics->setShaderUniform("u_fogData.end",   renderData->fogEnd);

				graphics->setShaderUniform("u_fogData.enabled", true);
            } else {
				graphics->setShaderUniform("u_fogData.enabled", false);
            }
            /*
            if (item->renderStates.receiveShadows && scene->shadowEnabled) {
                program->setUniformValue("u_shadowMap", 8);
                program->setUniformValue("u_shadowEnabled", true);
            } else {
                program->setUniformValue("u_shadowEnabled", false);
            }


            gl->glActiveTexture(GL_TEXTURE8);
            gl->glBindTexture(GL_TEXTURE_2D, shadowDepthMap);

            program->setUniformValue("u_lightSpaceMatrix",  lightSpaceMatrix);
            */
			graphics->setShaderUniform("u_lightCount",        lightCount);
            int shadowIndex = 8;
            // only materials get lights passed to it
            if ( item->renderStates.receiveLighting ) {
                for (int i=0;i<lightCount;i++)
                {
					auto& lightNames = this->lightUniformNames[i];
                    //QString lightPrefix = QString("u_lights[%0].").arg(i);

                    auto light = renderData->scene->lights[i];
                    if(!light->isVisible())
                    {
                        //quick hack for now
						graphics->setShaderUniform(lightNames.color.c_str(), QColor(0,0,0));
                        continue;
                    }

					graphics->setShaderUniform(lightNames.type.c_str(), (int)light->lightType);
					graphics->setShaderUniform(lightNames.position.c_str(), light->globalTransform.column(3).toVector3D());
                    //mat->setUniformValue(lightPrefix+"direction", light->getDirection());
					graphics->setShaderUniform(lightNames.distance.c_str(), light->distance);
					graphics->setShaderUniform(lightNames.direction.c_str(), light->getLightDir());
					graphics->setShaderUniform(lightNames.cutOffAngle.c_str(), light->spotCutOff);
					graphics->setShaderUniform(lightNames.cutOffSoftness.c_str(), light->spotCutOffSoftness);
					graphics->setShaderUniform(lightNames.intensity.c_str(), light->intensity);
					graphics->setShaderUniform(lightNames.color.c_str(), light->color);

					graphics->setShaderUniform(lightNames.shadowColor.c_str(), light->shadowColor);
					graphics->setShaderUniform(lightNames.shadowAlpha.c_str(), light->shadowAlpha);

					graphics->setShaderUniform(lightNames.constantAtten.c_str(), 1.0f);
					graphics->setShaderUniform(lightNames.linearAtten.c_str(), 0.0f);
					graphics->setShaderUniform(lightNames.quadAtten.c_str(), 1.0f);

                    // shadow data
//                    mat->setUniformValue(lightPrefix+"shadowEnabled",
//                                         item->renderStates.receiveShadows &&
//                                         scene->shadowEnabled &&
//                                         light->lightType != iris::LightType::Point);
					if (!scene->shadowEnabled) {
						graphics->setShaderUniform(lightNames.shadowType.c_str(), (int)iris::ShadowMapType::None);
					}
					else {
						graphics->setShaderUniform(lightNames.shadowMap.c_str(), shadowIndex);
						//mat->setUniformValue(QString("shadowMaps[%0].").arg(i), 8);
						graphics->setShaderUniform(lightNames.shadowMatrix.c_str(), light->shadowMap->shadowMatrix);
						if (light->lightType == iris::LightType::Point)
							graphics->setShaderUniform(lightNames.shadowType.c_str(), (int)iris::ShadowMapType::None);
						else
							graphics->setShaderUniform(lightNames.shadowType.c_str(), (int)light->shadowMap->shadowType);


						graphics->setTexture(shadowIndex, light->shadowMap->shadowTexture);
						shadowIndex++;
					}
                    //shadowDepthMap
                    //gl->glActiveTexture(GL_TEXTURE8);
                    //gl->glBindTexture(GL_TEXTURE_2D, light->shadowMap->shadowTexId);
                    //gl->glBindTexture(GL_TEXTURE_2D, shadowDepthMap);
                }
            }

            // set render states
            graphics->setRasterizerState(item->renderStates.rasterState);
            graphics->setDepthState(item->renderStates.depthState);
            graphics->setBlendState(item->renderStates.blendState);

            //item->mesh->draw(gl, program);
			item->mesh->draw(graphics);

            if (!!mat) {
                mat->end(graphics, scene);
            }
        }
        else if(item->type == iris::RenderItemType::ParticleSystem) {
            auto ps = item->sceneNode.staticCast<ParticleSystemNode>();
            ps->renderParticles(graphics, renderData, particleShader);
        }
    }

}

void ForwardRenderer::renderSky(RenderData* renderData)
{
    if (scene->skyMesh == nullptr) return;

    gl->glDepthMask(false);

    scene->skyMaterial->begin(graphics, scene);

	/*
    auto program = scene->skyMaterial->program;
    program->setUniformValue("u_viewMatrix",renderData->viewMatrix);
    program->setUniformValue("u_projMatrix",renderData->projMatrix);
    QMatrix4x4 worldMatrix;
    worldMatrix.setToIdentity();
    program->setUniformValue("u_worldMatrix",worldMatrix);
	*/

	graphics->setShader(scene->skyMaterial->shader);
	graphics->setShaderUniform("u_viewMatrix", renderData->viewMatrix);
	graphics->setShaderUniform("u_projMatrix", renderData->projMatrix);
	QMatrix4x4 worldMatrix;
	worldMatrix.setToIdentity();
	graphics->setShaderUniform("u_worldMatrix", worldMatrix);

    //scene->skyMesh->draw(gl,program);
    scene->skyMesh->draw(graphics);
    scene->skyMaterial->end(graphics, scene);

    gl->glDepthMask(true);
}

//todo: use render states
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
            billboard->draw(graphics);
        }
        else
        {
            gl->glBindTexture(GL_TEXTURE_2D,0);
        }


    }
    gl->glDisable(GL_BLEND);

    gl->glEnable(GL_CULL_FACE);
}

// http://gamedev.stackexchange.com/questions/59361/opengl-get-the-outline-of-multiple-overlapping-objects
void ForwardRenderer::renderSelectedNode(RenderData* renderData, SceneNodePtr node)
{
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

    //meshNode->mesh->draw(gl, shader);
    renderOutlineNode(renderData, node);

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

    //meshNode->mesh->draw(gl, shader);
    renderOutlineNode(renderData, node);

    gl->glDisable(GL_STENCIL_TEST);

    gl->glStencilMask(1);
    gl->glLineWidth(1);
    gl->glPolygonMode(GL_FRONT, GL_FILL);
}

void ForwardRenderer::renderOutlineNode(RenderData *renderData, SceneNodePtr node)
{
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();

        if (meshNode->mesh != nullptr) {
            QOpenGLShaderProgram* shader;
            if(meshNode->mesh->hasSkeleton())
                shader = skinnedLineShader;
            else
                shader = lineShader;

            shader->bind();

            shader->setUniformValue("u_worldMatrix",    node->globalTransform);
            shader->setUniformValue("u_viewMatrix",     renderData->viewMatrix);
            shader->setUniformValue("u_projMatrix",     renderData->projMatrix);
            shader->setUniformValue("u_normalMatrix",   node->globalTransform.normalMatrix());
            shader->setUniformValue("color",            scene->outlineColor);

            if(meshNode->mesh->hasSkeleton()) {
                auto boneTransforms = meshNode->mesh->getSkeleton()->boneTransforms;
                shader->setUniformValueArray("u_bones",          boneTransforms.data(), boneTransforms.size());
            }

            //meshNode->mesh->draw(gl, shader);
            meshNode->mesh->draw(graphics);
        }
    }

    for(auto childNode : node->children) {
		if (childNode->isVisible())
			renderOutlineNode(renderData, childNode);
    }
}

void ForwardRenderer::renderOutlineLine(RenderData *renderData, SceneNodePtr node)
{

}

void ForwardRenderer::createLineShader()
{
    lineShader = GraphicsHelper::loadShader(":assets/shaders/color.vert",
                                              ":assets/shaders/color.frag");

    lineShader->bind();
    lineShader->setUniformValue("color",QColor(240,240,255,255));

    skinnedLineShader = GraphicsHelper::loadShader(":assets/shaders/skinned_color.vert",
                                              ":assets/shaders/color.frag");

    skinnedLineShader->bind();
    skinnedLineShader->setUniformValue("color",QColor(240,240,255,255));
}

void ForwardRenderer::createShadowShader()
{
    skinnedShadowShader = GraphicsHelper::loadShader(":assets/shaders/skinned_shadow_map.vert",
                                                    ":assets/shaders/shadow_map.frag");

    shadowShader = GraphicsHelper::loadShader(":assets/shaders/shadow_map.vert",
                                              ":assets/shaders/shadow_map.frag");

    shadowShader->bind();
}

void ForwardRenderer::createParticleShader()
{
	/*
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile(":/assets/shaders/particle.vert");

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile(":/assets/shaders/particle.frag");

    particleShader = new QOpenGLShaderProgram;
    particleShader->addShader(vshader);
    particleShader->addShader(fshader);

    particleShader->link();

    particleShader->bind();
	*/
	particleShader = Shader::load(":/assets/shaders/particle.vert", ":/assets/shaders/particle.frag");
}

void ForwardRenderer::createEmitterShader()
{
    emitterShader = GraphicsHelper::loadShader(":/assets/shaders/emitter.vert",
                                               ":/assets/shaders/emitter.frag");
}

void ForwardRenderer::generateLightUnformNames()
{
	for (int i = 0; i < 16; i++) {
		LightUniformNames names;
		QString lightPrefix = QString("u_lights[%0].").arg(i);
		names.color = (lightPrefix + "color").toStdString();
		names.type = (lightPrefix + "type").toStdString();
		names.position = (lightPrefix + "position").toStdString();
		names.distance = (lightPrefix + "distance").toStdString();
		names.direction = (lightPrefix + "direction").toStdString();
		names.cutOffAngle = (lightPrefix + "cutOffAngle").toStdString();
		names.cutOffSoftness = (lightPrefix + "cutOffSoftness").toStdString();
		names.intensity = (lightPrefix + "intensity").toStdString();
		names.shadowColor = (lightPrefix + "shadowColor").toStdString();
		names.shadowAlpha = (lightPrefix + "shadowAlpha").toStdString();
		names.constantAtten = (lightPrefix + "constantAtten").toStdString();
		names.linearAtten = (lightPrefix + "linearAtten").toStdString();
		names.quadAtten = (lightPrefix + "quadtraticAtten").toStdString();
		names.shadowType = (lightPrefix + "shadowType").toStdString();
		names.shadowMap = (lightPrefix + "shadowMap").toStdString();
		names.shadowMatrix = (lightPrefix + "shadowMatrix").toStdString();
		lightUniformNames.append(names);
	}
}

ForwardRenderer::~ForwardRenderer()
{
    delete vrDevice;
}

}
