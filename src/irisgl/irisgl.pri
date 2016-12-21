#**************************************************************************
#This file is part of IrisGL
#http://www.irisgl.org
#Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>
#
#This is free software: you may copy, redistribute
#and/or modify it under the terms of the GPLv3 License
#
#For more information see the LICENSE file
#**************************************************************************

HEADERS += \
    $$PWD/src/core/scene.h \
    $$PWD/src/core/scenenode.h \
    $$PWD/src/animation/nodekeyframe.h \
    $$PWD/src/animation/keyframeanimation.h \
    $$PWD/src/scenegraph/lightnode.h \
    $$PWD/src/graphics/texture2d.h \
    $$PWD/src/graphics/texture.h \
    $$PWD/src/graphics/mesh.h \
    $$PWD/src/graphics/material.h \
    $$PWD/src/scenegraph/cameranode.h \
    $$PWD/src/scenegraph/meshnode.h \
    $$PWD/src/graphics/renderdata.h \
    $$PWD/src/graphics/forwardrenderer.h \
    $$PWD/src/materials/defaultmaterial.h \
    $$PWD/src/graphics/vertexlayout.h \
    $$PWD/src/iris.h \
    $$PWD/src/graphics/viewport.h \
    $$PWD/src/materials/billboardmaterial.h \
    $$PWD/src/graphics/graphicshelper.h \
    $$PWD/src/graphics/utils/billboard.h \
    $$PWD/src/geometry/trimesh.h \
    $$PWD/src/materials/defaultskymaterial.h \
    $$PWD/src/core/meshmanager.h \
    $$PWD/src/graphics/utils/fullscreenquad.h \
    $$PWD/src/vr/vrdevice.h \
    $$PWD/src/math/mathhelper.h \
    $$PWD/src/irisglfwd.h \
    $$PWD/src/graphics/shader.h \
    $$PWD/src/irisgl.h

include(src/assimp/assimp.pri)
include(src/libovr/libovr.pri)

SOURCES += \
    $$PWD/src/graphics/mesh.cpp \
    $$PWD/src/materials/defaultmaterial.cpp \
    $$PWD/src/core/scene.cpp \
    $$PWD/src/scenegraph/meshnode.cpp \
    $$PWD/src/core/scenenode.cpp \
    $$PWD/src/graphics/forwardrenderer.cpp \
    $$PWD/src/graphics/graphicshelper.cpp \
    $$PWD/src/graphics/utils/billboard.cpp \
    $$PWD/src/scenegraph/cameranode.cpp \
    $$PWD/src/graphics/texture2d.cpp \
    $$PWD/src/materials/defaultskymaterial.cpp \
    $$PWD/src/graphics/material.cpp \
    $$PWD/src/graphics/utils/fullscreenquad.cpp \
    $$PWD/src/vr/vrdevice.cpp \
    $$PWD/src/geometry/trimesh.cpp \
    $$PWD/src/graphics/vertexlayout.cpp \
    $$PWD/src/graphics/shader.cpp \
    $$PWD/src/graphics/texture.cpp
