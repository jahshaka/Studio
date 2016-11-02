#**************************************************************************
#This file is part of JahshakaVR, VR Authoring Toolkit
#http://www.jahshaka.com
#Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>
#
#This is free software: you may copy, redistribute
#and/or modify it under the terms of the GPLv3 License
#
#For more information see the LICENSE file
#**************************************************************************

#-------------------------------------------------
#
# Project created by QtCreator 2016-03-05T01:04:48
#
#-------------------------------------------------

QT  +=  3dcore 3drender 3dinput 3dquick 3dlogic 3dextras
QT       += core gui


#needed for c++11 support
#http://stackoverflow.com/questions/16948382/how-to-enable-c11-in-qt-creator
QMAKE_CXXFLAGS += -std=c++11

#needed to fix resource compilation error in visual studio
#http://stackoverflow.com/questions/28426240/qt-compiler-is-out-of-heap-space
CONFIG += resources_big

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = JahshakaVR
TEMPLATE = app


SOURCES += src/main.cpp\
        src/mainwindow.cpp \
    src/materials/flatshadedmaterial.cpp \
    src/materials/colormaterial.cpp \
    src/materials/endlessplanematerial.cpp \
    src/materials/billboardmaterial.cpp \
    src/materials/reflectivematerial.cpp \
    src/materials/refractivematerial.cpp \
    src/widgets/colorvaluewidget.cpp \
    src/widgets/colorpickerwidget.cpp \
    src/dialogs/preferencesdialog.cpp \
    src/dialogs/aboutdialog.cpp \
    src/dialogs/licensedialog.cpp \
    src/materials/advancedmaterial.cpp \
    src/dialogs/preferences/worldsettings.cpp \
    src/materials/materialpresets.cpp \
    src/dialogs/loadmeshdialog.cpp \
    src/materials/materialproxy.cpp \
    src/materials/materials.cpp \
    src/widgets/animationwidget.cpp \
    src/widgets/endlessplanelayerwidget.cpp \
    src/widgets/keyframelabelwidget.cpp \
    src/widgets/keyframewidget.cpp \
    src/widgets/lcdslider.cpp \
    src/widgets/lightlayerwidget.cpp \
    src/widgets/maintimelinewidget.cpp \
    src/widgets/materialwidget.cpp \
    src/widgets/modellayerwidget.cpp \
    src/widgets/spherelayerwidget.cpp \
    src/widgets/texturedplanelayerwidget.cpp \
    src/widgets/timelinewidget.cpp \
    src/widgets/toruslayerwidget.cpp \
    src/widgets/transformwidget.cpp \
    src/widgets/worldlayerwidget.cpp \
    src/core/jahrenderer.cpp \
    src/core/nodekeyframeanimation.cpp \
    src/core/project.cpp \
    src/core/sceneparser.cpp \
    src/core/settingsmanager.cpp \
    src/core/surfaceview.cpp \
    src/editor/editorcameracontroller.cpp \
    src/scenegraph/scenemanager.cpp \
    src/scenegraph/scenenodes.cpp \
    src/scenegraph/skyboxnode.cpp \
    src/geometry/billboard.cpp \
    src/geometry/gridentity.cpp \
    src/geometry/gridrenderer.cpp \
    src/geometry/linerenderer.cpp \
    src/geometry/linestrip.cpp \
    src/editor/gizmos/advancedtransformgizmo.cpp \
    src/editor/gizmos/rotationgizmo.cpp \
    src/editor/gizmos/scalegizmo.cpp \
    src/editor/gizmos/transformgizmo.cpp \
    src/editor/gizmos/translationgizmo.cpp \
    src/dialogs/infodialog.cpp \
    src/globals.cpp \
    src/dialogs/renamelayerdialog.cpp \
    src/materials/gizmomaterial.cpp \
    src/widgets/materialsets.cpp \
    src/widgets/modelpresets.cpp \
    src/widgets/accordianbladewidget.cpp \
    src/widgets/hfloatslider.cpp

HEADERS  += src/mainwindow.h \
    src/dialogs/renamelayerdialog.h \
    src/materials/gizmomaterial.h \
    src/materials/flatshadedmaterial.h \
    src/materials/colormaterial.h \
    src/materials/endlessplanematerial.h \
    src/materials/billboardmaterial.h \
    src/materials/reflectivematerial.h \
    src/materials/refractivematerial.h \
    src/widgets/colorvaluewidget.h \
    src/widgets/colorpickerwidget.h \
    src/helpers/texturehelper.h \
    src/helpers/settingshelper.h \
    src/dialogs/preferencesdialog.h \
    src/dialogs/aboutdialog.h \
    src/dialogs/licensedialog.h \
    src/materials/advancematerial.h \
    src/dialogs/preferences/worldsettings.h \
    src/helpers/collisionhelper.h \
    src/dialogs/infodialog.h \
    src/materials/materialpresets.h \
    src/dialogs/loadmeshdialog.h \
    src/materials/materialproxy.h \
    src/materials/materials.h \
    src/widgets/animationwidget.h \
    src/widgets/endlessplanelayerwidget.h \
    src/widgets/keyframelabelwidget.h \
    src/widgets/keyframewidget.h \
    src/widgets/layertreewidget.h \
    src/widgets/lcdslider.h \
    src/widgets/lightlayerwidget.h \
    src/widgets/maintimelinewidget.h \
    src/widgets/materialwidget.h \
    src/widgets/modellayerwidget.h \
    src/widgets/scenenodetransformui.h \
    src/widgets/spherelayerwidget.h \
    src/widgets/texturedplanelayerwidget.h \
    src/widgets/timelinewidget.h \
    src/widgets/toruslayerwidget.h \
    src/widgets/transformwidget.h \
    src/widgets/worldlayerwidget.h \
    src/core/jahrenderer.h \
    src/core/keyframeanimation.h \
    src/core/keyframes.h \
    src/core/nodekeyframe.h \
    src/core/nodekeyframeanimation.h \
    src/core/project.h \
    src/core/sceneparser.h \
    src/core/settingsmanager.h \
    src/core/surfaceview.h \
    src/editor/editorcameracontroller.h \
    src/scenegraph/lightnode.h \
    src/scenegraph/scenemanager.h \
    src/scenegraph/scenenodes.h \
    src/scenegraph/skyboxnode.h \
    src/scenegraph/torusnode.h \
    src/geometry/billboard.h \
    src/geometry/endlessplane.h \
    src/geometry/gridentity.h \
    src/geometry/gridrenderer.h \
    src/geometry/linerenderer.h \
    src/geometry/linestrip.h \
    src/editor/gizmos/advancedtransformgizmo.h \
    src/editor/gizmos/gizmobase.h \
    src/editor/gizmos/gizmotransform.h \
    src/editor/gizmos/rotationgizmo.h \
    src/editor/gizmos/scalegizmo.h \
    src/editor/gizmos/transformgizmo.h \
    src/editor/gizmos/translationgizmo.h \
	src/dialogs/infodialog.h \
    src/globals.h \
    src/widgets/materialsets.h \
    src/widgets/modelpresets.h \
    src/widgets/accordianbladewidget.h \
    src/widgets/hfloatslider.h

FORMS    += \
    src/dialogs/renamelayerdialog.ui \
    src/widgets/colorvaluewidget.ui \
    src/widgets/colorpickerwidget.ui \
    src/dialogs/preferencesdialog.ui \
    src/dialogs/aboutdialog.ui \
    src/dialogs/licensedialog.ui \
    src/dialogs/preferences/worldsettings.ui \
    src/dialogs/infodialog.ui \
    src/dialogs/loadmeshdialog.ui \
    src/widgets/animationwidget.ui \
    src/widgets/endlessplanelayerwidget.ui \
    src/widgets/lcdslider.ui \
    src/widgets/lightlayerwidget.ui \
    src/widgets/maintimelinewidget.ui \
    src/widgets/materialwidget.ui \
    src/widgets/modellayerwidget.ui \
    src/widgets/namedvalueslider.ui \
    src/widgets/newlcdslider.ui \
    src/widgets/spherelayerwidget.ui \
    src/widgets/texturedplanelayerwidget.ui \
    src/widgets/toruslayerwidget.ui \
    src/widgets/transformsliders.ui \
    src/widgets/worldlayerwidget.ui \
    src/dialogs/infodialog.ui \
    src/newmainwindow.ui \
    src/widgets/materialsets.ui \
    src/widgets/modelpresets.ui \
    src/widgets/accordianbladewidget.ui \
    src/widgets/hfloatslider.ui

DISTFILES +=

RESOURCES += \
    shaders.qrc \
    icons.qrc \
    images.qrc \
    materials.qrc \
    models.qrc \
    textures.qrc \
    modelpresets.qrc

win32: RC_ICONS = icon.ico

install_assets.path = $$OUT_PWD/assets
install_assets.files = assets/*

install_content.path = $$OUT_PWD/app/content
install_content.files = app/content/*

INSTALLS += \
    install_assets \
    install_content
