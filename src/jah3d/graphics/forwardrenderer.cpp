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
#include "texture2d.h"

namespace jah3d
{

ForwardRenderer::ForwardRenderer(QOpenGLFunctions_3_2_Core* gl)
{
    this->gl = gl;
    renderData = new RenderData();

    billboard = new Billboard(gl);
    createLineShader();
}

QSharedPointer<ForwardRenderer> ForwardRenderer::create(QOpenGLFunctions_3_2_Core* gl)
{
    return QSharedPointer<ForwardRenderer>(new ForwardRenderer(gl));
}

//all scene's transform should be updated
void ForwardRenderer::renderScene(Viewport* vp)
{
    auto cam = scene->camera;

    //STEP 1: RENDER SCENE
    renderData->scene = scene;

    cam->setAspectRatio(vp->getAspectRatio());
    cam->updateCameraMatrices();

    renderData->projMatrix = cam->projMatrix;
    renderData->viewMatrix = cam->viewMatrix;
    renderData->eyePos = cam->globalTransform.column(3).toVector3D();
    //renderData->gl = gl;

    renderNode(renderData,scene->rootNode);

    //STEP 2: RENDER LINES (for e.g. light radius and the camera frustum)

    //STEP 3: RENDER BILLBOARD ICONS
    renderBillboardIcons(renderData);

    //STEP 4: RENDER SELECTED OBJECT
    if(!!selectedSceneNode)
        renderSelectedNode(renderData,selectedSceneNode);

    //STEP 5: RENDER GIZMOS
}

void ForwardRenderer::renderNode(RenderData* renderData,QSharedPointer<SceneNode> node)
{
    if(node->sceneNodeType==SceneNodeType::Mesh && node->isVisible())
    {
        //qDebug()<<node->getName()+" is "+(node->isVisible()?"visible":"invisible")<<endl;
        auto meshNode = node.staticCast<MeshNode>();
        auto mat = meshNode->material;

        auto program = mat->program;
        //program->bind();

        //bind textures
        /*
        auto textures = mat->textures;
        for(int i=0;i<textures.size();i++)
        {
            auto tex = textures[i];
            gl->glActiveTexture(GL_TEXTURE0+i);

            if(tex->texture!=nullptr)
            {
                tex->texture->bind(i);
                mat->program->setUniformValue(tex->name.toStdString().c_str(), i);
            }
            else
            {
                gl->glActiveTexture(GL_TEXTURE0+i);
                gl->glBindTexture(GL_TEXTURE_2D,0);
            }
        }
        */

        mat->begin(gl);

        //send transform and light data
        //QMatrix4x4 local;
        //local.setToIdentity();
        program->setUniformValue("u_worldMatrix",node->globalTransform);
        program->setUniformValue("u_viewMatrix",renderData->viewMatrix);
        program->setUniformValue("u_projMatrix",renderData->projMatrix);
        program->setUniformValue("u_normalMatrix",node->globalTransform.normalMatrix());

        program->setUniformValue("u_eyePos",renderData->eyePos);

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
            mat->setUniformValue(lightPrefix+"direction", light->getLightDir());
            mat->setUniformValue(lightPrefix+"cutOffAngle", light->spotCutOff);
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

//http://gamedev.stackexchange.com/questions/59361/opengl-get-the-outline-of-multiple-overlapping-objects
void ForwardRenderer::renderSelectedNode(RenderData* renderData,QSharedPointer<SceneNode> node)
{
    //todo: move these
#define OUTLINE_STENCIL_CHANNEL 1

    if(node->getSceneNodeType()==jah3d::SceneNodeType::Mesh)
    {
        auto meshNode = node.staticCast<jah3d::MeshNode>();

        if(meshNode->mesh!=nullptr)
        {
            lineShader->bind();
            //lindShader->setUniformValue("",renderData)
            lineShader->setUniformValue("u_worldMatrix",node->globalTransform);
            lineShader->setUniformValue("u_viewMatrix",renderData->viewMatrix);
            lineShader->setUniformValue("u_projMatrix",renderData->projMatrix);
            lineShader->setUniformValue("u_normalMatrix",node->globalTransform.normalMatrix());
            lineShader->setUniformValue("color",QColor(200,200,255,255));

            //STEP 1: DRAW STENCIL OF THE FILLED POLYGON
            gl->glClearStencil(0);//sets default stencil value to 0
            gl->glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
            gl->glPolygonMode(GL_FRONT,GL_FILL);//should be the default

            gl->glEnable(GL_STENCIL_TEST);

            gl->glStencilMask(1);//works the same as the color and depth mask
            gl->glStencilFunc(GL_ALWAYS,1,OUTLINE_STENCIL_CHANNEL);//test must always pass
            gl->glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);//GL_REPLACE for all becase a stencil value should always be written
            gl->glColorMask(0,0,0,0);//disable drawing to color buffer

            meshNode->mesh->draw(gl,lineShader);

            //STEP 2: DRAW MESH IN LINE MODE WITH A LINE WIDTH > 1 SO THE OUTLINE PASSES THE STENCIL TEST
            gl->glPolygonMode(GL_FRONT,GL_LINE);
            //gl->glCullFace(GL_BACK);
            //gl->glEnable(GL_CULL_FACE);
            gl->glLineWidth(5);

            //the default stencil value is 0, if the stencil value at a pixel is 1 that means thats where the solid
            //mesh was rendered. The line version should only be rendered where the stencil value is 0.
            gl->glStencilFunc(GL_EQUAL,0,OUTLINE_STENCIL_CHANNEL);
            gl->glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
            gl->glStencilMask(0);//disables writing to stencil buffer
            gl->glColorMask(1,1,1,1);//enable drawing to color buffer

            meshNode->mesh->draw(gl,lineShader);

            gl->glDisable(GL_STENCIL_TEST);

            gl->glStencilMask(1);
            gl->glLineWidth(1);
            gl->glPolygonMode(GL_FRONT,GL_FILL);
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

    //program->bindAttributeLocation("a_pos", 0);
    //program->bindAttributeLocation("a_texCoord", 1);
    //program->bindAttributeLocation("a_normal", 2);
    //program->bindAttributeLocation("a_tangent", 3);

    lineShader->link();

    lineShader->bind();
    lineShader->setUniformValue("color",QColor(240,240,255,255));
}

/*
void ForwardRenderer::createBillboardIconAssets()
{
    //create shader
    //billboardShader = GraphicsHelper::loadShader("app/shaders/billboard.vert","app/shaders/billboard.frag");

    //create mesh

}
*/

}
