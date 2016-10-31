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


CONFIG += c++14

#needed to fix resource compilation error in visual studio
#http://stackoverflow.com/questions/28426240/qt-compiler-is-out-of-heap-space
CONFIG += resources_big

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = JahshakaVR
TEMPLATE = app


SOURCES += src/main.cpp\
        src/mainwindow.cpp \
    src/widgets/colorvaluewidget.cpp \
    src/widgets/colorpickerwidget.cpp \
    src/dialogs/preferencesdialog.cpp \
    src/dialogs/aboutdialog.cpp \
    src/dialogs/licensedialog.cpp \
    src/dialogs/preferences/worldsettings.cpp \
    src/dialogs/loadmeshdialog.cpp \
    src/widgets/animationwidget.cpp \
    src/widgets/keyframelabelwidget.cpp \
    src/widgets/keyframewidget.cpp \
    src/widgets/lcdslider.cpp \
    src/widgets/maintimelinewidget.cpp \
    src/widgets/materialwidget.cpp \
    src/widgets/timelinewidget.cpp \
    src/core/nodekeyframeanimation.cpp \
    src/core/project.cpp \
    src/core/settingsmanager.cpp \
    src/core/surfaceview.cpp \
    src/editor/editorcameracontroller.cpp \
    src/dialogs/infodialog.cpp \
    src/globals.cpp \
    src/dialogs/renamelayerdialog.cpp \

HEADERS  += src/mainwindow.h \
    src/dialogs/renamelayerdialog.h \
    src/widgets/colorvaluewidget.h \
    src/widgets/colorpickerwidget.h \
    src/helpers/settingshelper.h \
    src/dialogs/preferencesdialog.h \
    src/dialogs/aboutdialog.h \
    src/dialogs/licensedialog.h \
    src/dialogs/preferences/worldsettings.h \
    src/helpers/collisionhelper.h \
    src/dialogs/infodialog.h \
    src/dialogs/loadmeshdialog.h \
    src/widgets/animationwidget.h \
    src/widgets/keyframelabelwidget.h \
    src/widgets/keyframewidget.h \
    src/widgets/layertreewidget.h \
    src/widgets/lcdslider.h \
    src/widgets/lightlayerwidget.h \
    src/widgets/maintimelinewidget.h \
    src/widgets/materialwidget.h \
    src/widgets/scenenodetransformui.h \
    src/widgets/timelinewidget.h \
    src/core/keyframeanimation.h \
    src/core/keyframes.h \
    src/core/nodekeyframe.h \
    src/core/nodekeyframeanimation.h \
    src/core/project.h \
    src/core/settingsmanager.h \
    src/core/surfaceview.h \
    src/editor/editorcameracontroller.h \
	src/dialogs/infodialog.h \
    src/globals.h

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
    src/newmainwindow.ui

DISTFILES +=

RESOURCES += \
    shaders.qrc \
    icons.qrc \
    images.qrc \
    materials.qrc \
    models.qrc \
    textures.qrc

win32: RC_ICONS = icon.ico

install_assets.path = $$OUT_PWD/assets
install_assets.files = assets/*

install_content.path = $$OUT_PWD/app/content
install_content.files = app/content/*

INSTALLS += \
    install_assets \
    install_content

include(src/jah3d/jah3d.pri)
