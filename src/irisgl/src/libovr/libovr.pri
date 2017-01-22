HEADERS += \
    $$PWD/Include/OVR_CAPI.h \
    $$PWD/Include/OVR_CAPI_Audio.h \
    $$PWD/Include/OVR_CAPI_D3D.h \
    $$PWD/Include/OVR_CAPI_GL.h \
    $$PWD/Include/OVR_CAPI_Keys.h \
    $$PWD/Include/OVR_ErrorCode.h \
    $$PWD/Include/OVR_Version.h \
    $$PWD/Include/Extras/OVR_CAPI_Util.h \
    $$PWD/Include/Extras/OVR_Math.h \
    $$PWD/Include/Extras/OVR_StereoProjection.h

SOURCES += \
    $$PWD/Src/OVR_CAPI_Util.cpp \
    $$PWD/Src/OVR_StereoProjection.cpp \
    $$PWD/Src/OVR_CAPIShim.c

!win32 {
    LIBS += -ldl
}

gcc{
    QMAKE_CFLAGS_WARN_ON += -Wattributes
    QMAKE_CXXFLAGS_WARN_ON = $$QMAKE_CFLAGS_WARN_ON
}

INCLUDEPATH += $$PWD/Include
CONFIG += c++14
