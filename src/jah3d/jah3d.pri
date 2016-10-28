HEADERS += \
    $$PWD/core/scene.h \
    $$PWD/core/scenenode.h \
    $$PWD/animation/nodekeyframe.h \
    $$PWD/animation/keyframeanimation.h \
    $$PWD/scenegraph/lightnode.h \
    $$PWD/graphics/texture2d.h \
    $$PWD/graphics/texture.h \
    $$PWD/graphics/mesh.h \
    $$PWD/graphics/shaderprogram.h \
    $$PWD/graphics/material.h \
    $$PWD/scenegraph/cameranode.h \
    $$PWD/scenegraph/meshnode.h \
    $$PWD/graphics/renderdata.h \
    $$PWD/graphics/forwardrenderer.h \
    $$PWD/materials/defaultmaterial.h \
    $$PWD/graphics/vertexlayout.h

include(assimp/assimp.pri)
include(libovr/libovr.pri)

SOURCES += \
    $$PWD/graphics/mesh.cpp \
    $$PWD/materials/defaultmaterial.cpp