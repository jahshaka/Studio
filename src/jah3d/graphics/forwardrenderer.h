#ifndef FORWARDRENDERER_H
#define FORWARDRENDERER_H


#include "../core/scene.h"
#include "../core/scenenode.h"
#include "renderdata.h"
#include "material.h"
#include <QOpenGLContext>

namespace jah3d
{

class ForwardRenderer
{
    QOpenGLContext gl;
    RenderData* renderData;

public:
    //all scene's transform should be updated
    void renderScene(ScenePtr scene)
    {
        auto cam = scene->camera;

        //gather lights
        renderData = new RenderData();//bad, no allocations per frame
        renderData->scene = scene;
        renderData->projMatrix = cam->projMatrix;
        renderData->viewMatrix = cam->viewMatrix;

        renderNode(renderData,scene->rootNode);
    }

private:
    void renderNode(RenderData* renderData,SceneNodePtr node)
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
                    program->setUniformValue(tex->name.toStdString().c_str(), i);
                }
                else
                {
                    gl->glActiveTexture(GL_TEXTURE0+i);
                    gl->glBindTexture(GL_TEXTURE_2D,0);
                }
            }

            meshNode->draw(gl,mat->program);


            mat->program->unbind();
        }
    }
};

}

#endif // FORWARDRENDERER_H
