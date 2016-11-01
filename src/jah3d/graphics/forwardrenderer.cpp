#include "forwardrenderer.h"
#include "../core/scene.h"
#include "../core/scenenode.h"
#include "../scenegraph/cameranode.h"
#include "../scenegraph/meshnode.h"
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
    renderData = new RenderData();//bad, no allocations per frame
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

        mat->program->bind();

        //bind textures
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

        meshNode->mesh->draw(gl,mat->program);


        mat->program->release();
    }
}

}
