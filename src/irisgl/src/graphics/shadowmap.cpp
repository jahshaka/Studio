#include "shadowmap.h"
#include "texture2d.h"
#include <QOpenGLFunctions_3_2_Core>

namespace iris
{

ShadowMap::ShadowMap()
{
    shadowType = ShadowMapType::Hard;
    resolution = 1024*4;
    shadowTexture = Texture2D::createShadowDepth(resolution, resolution);
    bias = 0.01f;
    /*
    auto gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    gl->glGenTextures(1, &shadowTexId);
    gl->glBindTexture(GL_TEXTURE_2D, shadowTexId);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32,
                     resolution, resolution,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    */
}

void ShadowMap::setResolution(int size)
{
    resolution = size;
    shadowTexture->resize(size, size);
}



}
