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

include(src/zip/zip.pri)
include(src/ziplib/ziplib.pri)
include(src/assimp/assimp.pri)
include(src/libovr/libovr.pri)

HEADERS += \
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
    $$PWD/src/irisgl.h \
    $$PWD/src/math/intersectionhelper.h \
    $$PWD/src/animation/keyframeset.h \
    $$PWD/src/math/bezierhelper.h \
    $$PWD/src/animation/animation.h \
    $$PWD/src/materials/materialhelper.h \
    $$PWD/src/core/irisutils.h \
    $$PWD/src/scenegraph/viewernode.h \
    $$PWD/src/materials/viewermaterial.h \
    $$PWD/src/graphics/particle.h \
    $$PWD/src/graphics/particlerender.h \
    $$PWD/src/graphics/renderitem.h \
    $$PWD/src/scenegraph/particlesystemnode.h \
    $$PWD/src/vr/vrmanager.h \
    $$PWD/src/graphics/iviewsource.h \
    $$PWD/src/materials/custommaterial.h \
    $$PWD/src/graphics/postprocess.h \
    $$PWD/src/graphics/postprocessmanager.h \
    $$PWD/src/graphics/rendertexture.h \
    $$PWD/src/graphics/rendertarget.h \
    $$PWD/src/postprocesses/bloompostprocess.h \
    $$PWD/src/postprocesses/coloroverlaypostprocess.h \
    $$PWD/src/postprocesses/radialblurpostprocess.h \
    $$PWD/src/postprocesses/greyscalepostprocess.h \
    $$PWD/src/postprocesses/ssaopostprocess.h \
    $$PWD/src/postprocesses/materialpostprocess.h \
    $$PWD/src/animation/propertyanim.h \
    $$PWD/src/animation/animableproperty.h \
    $$PWD/src/utils/hashedlist.h \
    $$PWD/src/graphics/skeleton.h \
    $$PWD/src/animation/skeletalanimation.h \
    $$PWD/src/animation/floatcurve.h \
    $$PWD/src/scenegraph/scene.h \
    $$PWD/src/scenegraph/scenenode.h \
    $$PWD/src/core/property.h \
    $$PWD/src/geometry/boundingsphere.h \
    $$PWD/src/geometry/plane.h \
    $$PWD/src/geometry/frustum.h \
    $$PWD/src/math/transform.h \
    $$PWD/src/core/logger.h \
    $$PWD/src/core/performancetimer.h \
    $$PWD/src/graphics/renderlist.h \
    $$PWD/src/graphics/renderstates.h \
    $$PWD/src/graphics/utils/linemeshbuilder.h \
    $$PWD/src/graphics/utils/shapehelper.h \
    $$PWD/src/materials/colormaterial.h \
    $$PWD/src/postprocesses/fxaapostprocess.h \
    $$PWD/src/graphics/graphicsdevice.h \
    $$PWD/src/widgets/renderwidget.h \
    $$PWD/src/graphics/spritebatch.h \
    $$PWD/src/graphics/font.h \
    $$PWD/src/graphics/blendstate.h \
    $$PWD/src/graphics/depthstate.h \
    $$PWD/src/graphics/rasterizerstate.h \
    $$PWD/src/content/contentmanager.h

SOURCES += \
    $$PWD/src/graphics/mesh.cpp \
    $$PWD/src/materials/defaultmaterial.cpp \
    $$PWD/src/scenegraph/meshnode.cpp \
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
    $$PWD/src/graphics/texture.cpp \
    $$PWD/src/animation/animation.cpp \
    $$PWD/src/animation/keyframeset.cpp \
    $$PWD/src/materials/materialhelper.cpp \
    $$PWD/src/scenegraph/viewernode.cpp \
    $$PWD/src/materials/viewermaterial.cpp \
    $$PWD/src/scenegraph/particlesystemnode.cpp \
    $$PWD/src/vr/vrmanager.cpp \
    $$PWD/src/math/mathhelper.cpp \
    $$PWD/src/materials/custommaterial.cpp \
    $$PWD/src/graphics/rendertarget.cpp \
    $$PWD/src/graphics/postprocessmanager.cpp \
    $$PWD/src/postprocesses/coloroverlaypostprocess.cpp \
    $$PWD/src/postprocesses/radialblurpostprocess.cpp \
    $$PWD/src/postprocesses/bloompostprocess.cpp \
    $$PWD/src/postprocesses/greyscalepostprocess.cpp \
    $$PWD/src/postprocesses/ssaopostprocess.cpp \
    $$PWD/src/animation/propertyanim.cpp \
    $$PWD/src/scenegraph/lightnode.cpp \
    $$PWD/src/animation/skeletalanimation.cpp \
    $$PWD/src/graphics/skeleton.cpp \
    $$PWD/src/scenegraph/scene.cpp \
    $$PWD/src/scenegraph/scenenode.cpp \
    $$PWD/src/geometry/plane.cpp \
    $$PWD/src/geometry/frustum.cpp \
    $$PWD/src/core/logger.cpp \
    $$PWD/src/graphics/renderlist.cpp \
    $$PWD/src/graphics/renderitem.cpp \
    $$PWD/src/graphics/utils/linemeshbuilder.cpp \
    $$PWD/src/graphics/utils/shapehelper.cpp \
    $$PWD/src/materials/colormaterial.cpp \
    $$PWD/src/postprocesses/fxaapostprocess.cpp \
    $$PWD/src/graphics/graphicsdevice.cpp \
    $$PWD/src/widgets/renderwidget.cpp \
    $$PWD/src/graphics/spritebatch.cpp \
    $$PWD/src/graphics/font.cpp \
    $$PWD/src/graphics/blendstate.cpp \
    $$PWD/src/graphics/depthstate.cpp \
    $$PWD/src/graphics/rasterizerstate.cpp \
    $$PWD/src/content/contentmanager.cpp

RESOURCES += \
    $$PWD/assets.qrc
