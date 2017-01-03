#include "forwardrenderer.h"
#include "../core/scene.h"
#include "../core/scenenode.h"
#include "../scenegraph/cameranode.h"
#include "../scenegraph/meshnode.h"
#include "../scenegraph/lightnode.h"
#include "mesh.h"
#include "graphicshelper.h"
#include "renderdata.h"
#include "material.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLTexture>
#include "viewport.h"
#include "utils/billboard.h"
#include "utils/fullscreenquad.h"
#include "texture2d.h"
#include "../vr/vrdevice.h"

#include <QOpenGLContext>
#include "../libovr/Include/OVR_CAPI_GL.h"
#include "../libovr/Include/Extras/OVR_Math.h"

using namespace OVR;

namespace iris
{

ForwardRenderer::ForwardRenderer(QOpenGLFunctions_3_2_Core* gl)
{
    this->gl = gl;
    this->GLA = gl;
    renderData = new RenderData();

    billboard = new Billboard(gl);
    fsQuad = new FullScreenQuad();
    createLineShader();

    vrDevice = new VrDevice(gl);
    vrDevice->initialize();
}

QSharedPointer<ForwardRenderer> ForwardRenderer::create(QOpenGLFunctions_3_2_Core* gl)
{
    return QSharedPointer<ForwardRenderer>(new ForwardRenderer(gl));
}

// all scene's transform should be updated
void ForwardRenderer::renderScene(QOpenGLContext* ctx,
                                  Viewport* vp,
                                  QMatrix4x4& viewMatrix,
                                  QMatrix4x4& projMatrix)
{
    auto cam = scene->camera;

    // STEP 1: RENDER SCENE
    renderData->scene = scene;

    cam->setAspectRatio(vp->getAspectRatio());
    cam->updateCameraMatrices();

    projMatrix = renderData->projMatrix = cam->projMatrix;
    viewMatrix = renderData->viewMatrix = cam->viewMatrix;
    renderData->eyePos = cam->globalTransform.column(3).toVector3D();

    renderData->fogColor = scene->fogColor;
    renderData->fogStart = scene->fogStart;
    renderData->fogEnd = scene->fogEnd;
    renderData->fogEnabled = scene->fogEnabled;

    //renderData->gl = gl;

    renderNode(renderData,scene->rootNode);

    // STEP 2: RENDER SKY
    renderSky(renderData);

    // STEP 3: RENDER LINES (for e.g. light radius and the camera frustum)

    // STEP 4: RENDER BILLBOARD ICONS
    renderBillboardIcons(renderData);

    // STEP 5: RENDER SELECTED OBJECT
    if (!!selectedSceneNode) renderSelectedNode(renderData,selectedSceneNode);
}

void ForwardRenderer::renderSceneVr(QOpenGLContext* ctx,Viewport* vp)
{
    if(!vrDevice->isVrSupported())
        return;

    auto camera = scene->camera;
    vrDevice->beginFrame();

    for (int eye = 0; eye < 2; ++eye)
    {
        vrDevice->beginEye(eye);

        auto view = vrDevice->getEyeViewMatrix(eye,camera->pos);
        renderData->eyePos = view.column(3).toVector3D();
        renderData->viewMatrix = view;

        auto proj = vrDevice->getEyeProjMatrix(eye,0.1f,1000.0f);
        renderData->projMatrix = proj;


        //STEP 1: RENDER SCENE
        renderData->scene = scene;

        camera->setAspectRatio(vp->getAspectRatio());
        camera->updateCameraMatrices();

        renderData->eyePos = camera->globalTransform.column(3).toVector3D();

        renderData->fogColor = scene->fogColor;
        renderData->fogStart = scene->fogStart;
        renderData->fogEnd = scene->fogEnd;
        renderData->fogEnabled = scene->fogEnabled;

        //renderData->gl = gl;

        renderNode(renderData,scene->rootNode);

        //STEP 2: RENDER SKY
        renderSky(renderData);


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

void ForwardRenderer::renderNode(RenderData* renderData,QSharedPointer<SceneNode> node)
{
    if(node->sceneNodeType==SceneNodeType::Mesh && node->isVisible())
    {
        //qDebug()<<node->getName()+" is "+(node->isVisible()?"visible":"invisible")<<endl;
        auto meshNode = node.staticCast<MeshNode>();
        auto mat = meshNode->material;

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

        meshNode->mesh->draw(gl,program);

        //mat->program->release();
    }

    for(auto childNode:node->children)
        renderNode(renderData,childNode);
}

void ForwardRenderer::renderSky(RenderData* renderData)
{
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
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile("app/shaders/color.vert");

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile("app/shaders/color.frag");


    lineShader = new QOpenGLShaderProgram;
    lineShader->addShader(vshader);
    lineShader->addShader(fshader);

    lineShader->link();

    lineShader->bind();
    lineShader->setUniformValue("color",QColor(240,240,255,255));
}

ForwardRenderer::~ForwardRenderer()
{
    delete vrDevice;
}

}
