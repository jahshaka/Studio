#include "forwardrenderer.h"
#include "../core/scene.h"
#include "../core/scenenode.h"
#include "../scenegraph/cameranode.h"
#include "../scenegraph/meshnode.h"
#include "../scenegraph/lightnode.h"
#include "../graphics/mesh.h"
#include "renderdata.h"
#include "material.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>

namespace jah3d
{

ForwardRenderer::ForwardRenderer(QOpenGLFunctions* gl)
{
    this->gl = gl;
    renderData = new RenderData();
}

QSharedPointer<ForwardRenderer> ForwardRenderer::create(QOpenGLFunctions* gl)
{
    return QSharedPointer<ForwardRenderer>(new ForwardRenderer(gl));
}

//all scene's transform should be updated
void ForwardRenderer::renderScene(QSharedPointer<Scene> scene)
{
    auto cam = scene->camera;

    //gather lights
    //renderData = new RenderData();//bad, no allocations per frame
    renderData->scene = scene;
    renderData->projMatrix = cam->projMatrix;
    renderData->viewMatrix = cam->viewMatrix;

    renderNode(renderData,scene->rootNode);
}

void ForwardRenderer::renderNode(RenderData* renderData,QSharedPointer<SceneNode> node)
{
    if(node->sceneNodeType==SceneNodeType::Mesh)
    {
        auto meshNode = node.staticCast<MeshNode>();
        auto mat = meshNode->material;

        QOpenGLShaderProgram* program = mat->program;
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

        program->setUniformValue("u_textureScale",1.0f);

        auto lightCount = renderData->scene->lights.size();
        mat->program->setUniformValue("u_lightCount",lightCount);

        for(int i=0;i<lightCount;i++)
        {
            auto light = renderData->scene->lights[i];

            QString lightPrefix = QString("u_lights[%0].").arg(i);
            mat->setUniformValue(lightPrefix+"type", (int)light->lightType);
            mat->setUniformValue(lightPrefix+"position", light->pos);
            //mat->setUniformValue(lightPrefix+"direction", light->getDirection());
            mat->setUniformValue(lightPrefix+"cutOffAngle", 30.0f);
            mat->setUniformValue(lightPrefix+"intensity", light->intensity);
            mat->setUniformValue(lightPrefix+"color", light->color);

            mat->setUniformValue(lightPrefix+"constantAtten", 1.0f);
            mat->setUniformValue(lightPrefix+"linearAtten", 0.0f);
            mat->setUniformValue(lightPrefix+"quadtraticAtten", 0.2f);
        }

        meshNode->mesh->draw(gl,program);

        //mat->program->release();
    }

    for(auto childNode:node->children)
        renderNode(renderData,childNode);
}

}
