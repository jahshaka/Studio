#**************************************************************************
#This file is part of JahshakaVR, VR Authoring Toolkit
#http://www.jahshaka.com
#Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>
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
    src/scenemanager.cpp \
    src/surfaceview.cpp \
    src/scenenode.cpp \
    src/nodekeyframeanimation.cpp \
    src/globals.cpp \
    src/modellayerwidget.cpp \
    src/lightlayerwidget.cpp \
    src/toruslayerwidget.cpp \
    src/spherelayerwidget.cpp \
    src/materialwidget.cpp \
    src/loadmeshdialog.cpp \
    src/sceneparser.cpp \
    src/animationwidget.cpp \
    src/materialproxy.cpp \
    src/texturedplanelayerwidget.cpp \
    src/maintimelinewidget.cpp \
    src/worldlayerwidget.cpp \
    src/endlessplanelayerwidget.cpp \
    src/linestrip.cpp \
    src/transformgizmo.cpp \
    src/billboard.cpp \
    src/keyframelabelwidget.cpp \
    src/keyframewidget.cpp \
    src/materials.cpp \
    src/timelinewidget.cpp \
    src/dialogs/renamelayerdialog.cpp \
    src/lcdslider.cpp \
    src/materials/gizmomaterial.cpp \
    src/linerenderer.cpp \
    src/materials/flatshadedmaterial.cpp \
    src/materials/colormaterial.cpp \
    src/materials/endlessplanematerial.cpp \
    src/materials/billboardmaterial.cpp \
    src/advancedtransformgizmo.cpp \
    src/scenenodes/skyboxnode.cpp \
    src/materials/reflectivematerial.cpp \
    src/materials/refractivematerial.cpp \
    src/gridentity.cpp \
    src/gridrenderer.cpp \
    src/widgets/colorvaluewidget.cpp \
    src/widgets/colorpickerwidget.cpp \
    src/project.cpp \
    src/transformwidget.cpp \
    src/dialogs/preferencesdialog.cpp \
    src/dialogs/aboutdialog.cpp \
    src/dialogs/licensedialog.cpp \
    src/particlesystem.cpp \
    src/materials/advancedmaterial.cpp \
    src/jahrenderer.cpp \
    src/editorcameracontroller.cpp \
    src/dialogs/preferences/worldsettings.cpp \
    src/gizmo/translationgizmo.cpp \
    src/gizmo/scalegizmo.cpp \
    src/gizmo/rotationgizmo.cpp \
    src/dialogs/infodialog.cpp \
    src/settingsmanager.cpp \
    src/materials/materialpresets.cpp
    src/dialogs/infodialog.cpp

HEADERS  += src/mainwindow.h \
    src/scenemanager.h \
    src/surfaceview.h \
    src/nodekeyframe.h \
    src/scenenode.h \
    src/nodekeyframeanimation.h \
    src/globals.h \
    src/keyframeanimation.h \
    src/editorcameracontroller.h \
    src/scenenodetransformui.h \
    src/keyframes.h \
    src/modellayerwidget.h \
    src/lightlayerwidget.h \
    src/toruslayerwidget.h \
    src/spherelayerwidget.h \
    src/materialwidget.h \
    src/loadmeshdialog.h \
    src/sceneparser.h \
    src/animationwidget.h \
    src/keyframewidget.h \
    src/timelinewidget.h \
    src/keyframelabelwidget.h \
    src/materialproxy.h \
    src/texturedplanelayerwidget.h \
    src/maintimelinewidget.h \
    src/worldlayerwidget.h \
    src/materials.h \
    src/endlessplane.h \
    src/endlessplanelayerwidget.h \
    src/linestrip.h \
    src/transformgizmo.h \
    src/skies.h \
    src/billboard.h \
    src/scenenodes/lightnode.h \
    src/scenenodes/torusnode.h \
    src/dialogs/renamelayerdialog.h \
    src/layertreewidget.h \
    src/lcdslider.h \
    src/materials/gizmomaterial.h \
    src/linerenderer.h \
    src/materials/flatshadedmaterial.h \
    src/materials/colormaterial.h \
    src/materials/endlessplanematerial.h \
    src/materials/billboardmaterial.h \
    src/advancedtransformgizmo.h \
    src/materials/reflectivematerial.h \
    src/scenenodes/skyboxnode.h \
    src/materials/refractivematerial.h \
    src/gridrenderer.h \
    src/gridentity.h \
    src/widgets/colorvaluewidget.h \
    src/widgets/colorpickerwidget.h \
    src/project.h \
    src/transformwidget.h \
    src/helpers/texturehelper.h \
    src/helpers/settingshelper.h \
    src/settingsmanager.h \
    src/dialogs/preferencesdialog.h \
    src/dialogs/aboutdialog.h \
    src/dialogs/licensedialog.h \
    src/particlesystem.h \
    src/materials/advancematerial.h \
    src/jahrenderer.h \
    src/gizmobase.h \
    src/dialogs/preferences/worldsettings.h \
    src/helpers/collisionhelper.h \
    src/gizmo/translationgizmo.h \
    src/gizmo/gizmotransform.h \
    src/gizmo/scalegizmo.h \
    src/gizmo/rotationgizmo.h \
    src/dialogs/infodialog.h \
    src/materials/materialpresets.h
	src/dialogs/infodialog.h

FORMS    += src/mainwindow.ui \
    src/namedvalueslider.ui \
    src/transformsliders.ui \
    src/modellayerwidget.ui \
    src/lightlayerwidget.ui \
    src/toruslayerwidget.ui \
    src/spherelayerwidget.ui \
    src/materialwidget.ui \
    src/loadmeshdialog.ui \
    src/animationwidget.ui \
    src/texturedplanelayerwidget.ui \
    src/maintimelinewidget.ui \
    src/worldlayerwidget.ui \
    src/endlessplanelayerwidget.ui \
    src/newmainwindow.ui \
    src/dialogs/renamelayerdialog.ui \
    src/lcdslider.ui \
    src/widgets/colorvaluewidget.ui \
    src/widgets/colorpickerwidget.ui \
    src/dialogs/preferencesdialog.ui \
    src/dialogs/aboutdialog.ui \
    src/dialogs/licensedialog.ui \
    src/dialogs/preferences/worldsettings.ui \
    src/dialogs/infodialog.ui \
    src/newlcdslider.ui
    src/newlcdslider.ui
    src/dialogs/infodialog.ui

DISTFILES +=

RESOURCES += \
    shaders.qrc \
    icons.qrc \
    images.qrc \
    materials.qrc

win32: RC_ICONS = icon.ico

install_assets.path = $$OUT_PWD/assets
install_assets.files = assets/*

INSTALLS += \
    install_assets
