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
#include <QOpenGLTexture>
#include "viewport.h"
#include "utils/billboard.h"
#include "texture2d.h"

namespace jah3d
{

ForwardRenderer::ForwardRenderer(QOpenGLFunctions* gl)
{
    this->gl = gl;
    renderData = new RenderData();

    billboard = new Billboard(gl);
}

QSharedPointer<ForwardRenderer> ForwardRenderer::create(QOpenGLFunctions* gl)
{
    return QSharedPointer<ForwardRenderer>(new ForwardRenderer(gl));
}

//all scene's transform should be updated
void ForwardRenderer::renderScene(Viewport* vp,QSharedPointer<Scene> scene)
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

    //STEP 4: RENDER GIZMOS
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

/*
void ForwardRenderer::createBillboardIconAssets()
{
    //create shader
    //billboardShader = GraphicsHelper::loadShader("app/shaders/billboard.vert","app/shaders/billboard.frag");

    //create mesh

}
*/

}
