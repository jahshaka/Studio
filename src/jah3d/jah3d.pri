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
    $$PWD/graphics/vertexlayout.h \
    $$PWD/jah3d.h \
    $$PWD/graphics/viewport.h \
    $$PWD/materials/billboardmaterial.h \
    $$PWD/graphics/graphicshelper.h \
    $$PWD/graphics/utils/billboard.h \
    $$PWD/geometry/trimesh.h \
    $$PWD/materials/defaultskymaterial.h \
    $$PWD/core/meshmanager.h

include(assimp/assimp.pri)
include(libovr/libovr.pri)

SOURCES += \
    $$PWD/graphics/mesh.cpp \
    $$PWD/materials/defaultmaterial.cpp \
    $$PWD/core/scene.cpp \
    $$PWD/scenegraph/meshnode.cpp \
    $$PWD/core/scenenode.cpp \
    $$PWD/graphics/forwardrenderer.cpp \
    $$PWD/graphics/graphicshelper.cpp \
    $$PWD/graphics/utils/billboard.cpp \
    $$PWD/scenegraph/cameranode.cpp \
    $$PWD/graphics/texture2d.cpp \
    $$PWD/materials/defaultskymaterial.cpp \
    $$PWD/graphics/material.cpp