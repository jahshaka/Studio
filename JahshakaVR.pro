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

#QT  +=  3dcore 3drender 3dinput 3dquick 3dlogic 3dextras
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
    src/widgets/sceneviewwidget.cpp \
    src/widgets/materialsets.cpp \
    src/widgets/modelpresets.cpp \
    src/widgets/accordianbladewidget.cpp \
    src/widgets/hfloatslider.cpp \
    src/widgets/texturepicker.cpp \
    src/widgets/transformeditor.cpp \
    src/widgets/sceneheirarchywidget.cpp \
    src/editor/cameracontrollerbase.cpp \
    src/widgets/skypresets.cpp \
    src/editor/orbitalcameracontroller.cpp \
    src/widgets/scenenodepropertieswidget.cpp \
    src/widgets/propertywidgets/lightpropertywidget.cpp \
    src/widgets/propertywidgets/materialpropertywidget.cpp \
    src/widgets/filepickerwidget.cpp \
    src/widgets/propertywidgets/worldpropertywidget.cpp \
    src/io/scenewriter.cpp \
    src/io/sceneiobase.cpp

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
    src/globals.h \
    src/widgets/sceneviewwidget.h \
    src/globals.h \
    src/widgets/materialsets.h \
    src/widgets/modelpresets.h \
    src/widgets/accordianbladewidget.h \
    src/widgets/hfloatslider.h \
    src/widgets/texturepicker.h \
    src/widgets/transformeditor.h \
    src/widgets/sceneheirarchywidget.h \
    src/editor/orbitalcameracontroller.h \
    src/editor/cameracontrollerbase.h \
    src/widgets/skypresets.h \
    src/widgets/propertywidgets/transformpropertywidget.h \
    src/widgets/propertywidgets/lightpropertywidget.h \
    src/widgets/propertywidgets/materialpropertywidget.h \
    src/widgets/scenenodepropertieswidget.h \
    src/widgets/filepickerwidget.h \
    src/widgets/propertywidgets/worldpropertywidget.h \
    src/io/scenewriter.h \
    src/io/sceneiobase.h \
    src/io/scenereader.h \
    src/widgets/propertywidgets/scenepropertywidget.h

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
    src/widgets/hfloatslider.ui \
    src/widgets/texturepicker.ui \
    src/widgets/transformeditor.ui \
    src/widgets/sceneheirarchywidget.ui \
    src/widgets/skypresets.ui \
    src/widgets/filepickerwidget.ui

RESOURCES += \
    shaders.qrc \
    icons.qrc \
    images.qrc \
    materials.qrc \
    models.qrc \
    textures.qrc \
    modelpresets.qrc

win32: RC_ICONS = icon.ico

#http://stackoverflow.com/questions/19066593/copy-a-file-to-build-directory-after-compiling-project-with-qt/39234363#39234363
moveassets.commands = $(COPY_DIR) \"$$shell_path($$PWD/assets)\" \"$$shell_path($$OUT_PWD/assets)\"
movecontent.commands = $(COPY_DIR) \"$$shell_path($$PWD/app)\" \"$$shell_path($$OUT_PWD/app)\"

first.depends = $(first) moveassets movecontent
export(first.depends)
export(movecontent.commands)
export(moveassets.commands)
QMAKE_EXTRA_TARGETS += first moveassets movecontent

include(src/jah3d/jah3d.pri)

DISTFILES += \
    app/shaders/defaultsky.vert \
    app/shaders/defaultsky.fsh
