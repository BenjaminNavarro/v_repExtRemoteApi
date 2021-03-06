QT -= core
QT -= gui

TARGET = v_repExtRemoteApi
TEMPLATE = lib

DEFINES -= UNICODE
DEFINES += QT_COMPIL
CONFIG += shared
INCLUDEPATH += "../include"

*-msvc* {
    QMAKE_CXXFLAGS += -O2
    QMAKE_CXXFLAGS += -W3
}
*-g++* {
    QMAKE_CXXFLAGS += -O3
    QMAKE_CXXFLAGS += -Wall
    QMAKE_CXXFLAGS += -Wno-unused-parameter
    QMAKE_CXXFLAGS += -Wno-strict-aliasing
    QMAKE_CXXFLAGS += -Wno-empty-body
    QMAKE_CXXFLAGS += -Wno-write-strings

    QMAKE_CXXFLAGS += -Wno-unused-but-set-variable
    QMAKE_CXXFLAGS += -Wno-unused-local-typedefs
    QMAKE_CXXFLAGS += -Wno-narrowing

    QMAKE_CFLAGS += -O3
    QMAKE_CFLAGS += -Wall
    QMAKE_CFLAGS += -Wno-strict-aliasing
    QMAKE_CFLAGS += -Wno-unused-parameter
    QMAKE_CFLAGS += -Wno-unused-but-set-variable
    QMAKE_CFLAGS += -Wno-unused-local-typedefs
}


win32 {
    DEFINES += WIN_VREP
    LIBS += -lwinmm
    LIBS += -lWs2_32
    LIBS += -lKernel32
}

macx {
    DEFINES += MAC_VREP
#    LIBS += -lrt
}

unix:!macx {
    DEFINES += LIN_VREP
    LIBS += -lrt
}

SOURCES += \
    v_repExtRemoteApi.cpp \
    confReader.cpp \
    inConnection.cpp \
    porting.cpp \
    simxCmd.cpp \
    simxConnections.cpp \
    simxContainer.cpp \
    simxSocket.cpp \
    simxUtils.cpp \
    ../common/scriptFunctionData.cpp \
    ../common/scriptFunctionDataItem.cpp \
    ../common/shared_memory.c \
    ../common/v_repLib.cpp \

HEADERS +=\
    v_repExtRemoteApi.h \
    confReader.h \
    inConnection.h \
    porting.h \
    simxCmd.h \
    simxConnections.h \
    simxContainer.h \
    simxSocket.h \
    simxUtils.h \
    ../include/scriptFunctionData.h \
    ../include/scriptFunctionDataItem.h \
    ../include/shared_memory.h \
    ../include/v_repLib.h \

unix:!symbian {
    maemo5 {
        target.path = /opt/usr/lib
    } else {
        target.path = /usr/lib
    }
    INSTALLS += target
}
