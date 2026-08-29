cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(ShaderGraph)

set (CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(EFFECT_BUILD_AS_LIB false CACHE BOOL "Builds the lib as a static library to be embedded in Jahshaka")

#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt5 REQUIRED COMPONENTS Widgets Core Sql)

if (NOT EFFECT_BUILD_AS_LIB)
	add_subdirectory(irisgl)
endif (NOT EFFECT_BUILD_AS_LIB)

set_target_properties(IrisGL PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(assimp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CPP
	core/materialhelper.cpp
	core/materialwriter.cpp
	core/pbrgraphevaluator.cpp
	#core/project.cpp
	core/texturemanager.cpp
	core/undoredo.cpp
	core/sockethelper.cpp

	dialogs/createnewdialog.cpp
	dialogs/searchdialog.cpp

	generator/shadercontext.cpp
	generator/shadergenerator.cpp

	graph/graphnode.cpp
	graph/graphnodescene.cpp
	graph/nodegraph.cpp
	graph/socket.cpp
	graph/socketconnection.cpp

	misc/QtAwesome.cpp
	misc/QtAwesomeAnim.cpp

	models/connectionmodel.cpp
	models/library.cpp
	models/libraryv1.cpp
	models/nodemodel.cpp
	models/properties.cpp
	models/socketmodel.cpp

	nodes/inputs.cpp
	nodes/math.cpp
	nodes/object.cpp
	nodes/pbrmasternode.cpp
	nodes/test.cpp
	nodes/texture.cpp
	nodes/utils.cpp
	nodes/vector.cpp
	nodes/view.cpp

	propertywidgets/floatpropertywidget.cpp
    propertywidgets/vectorpropertywidget.cpp
    propertywidgets/intpropertywidget.cpp
    propertywidgets/texturepropertywidget.cpp
    propertywidgets/basepropertywidget.cpp
    propertywidgets/propertywidgetbase.cpp

	widgets/graphicsview.cpp
	widgets/listwidget.cpp
	widgets/materialsettingswidget.cpp
	widgets/propertylistwidget.cpp
	widgets/scenewidget.cpp
	widgets/shaderlistwidget.cpp
	widgets/shaderassetwidget.cpp
	widgets/treewidget.cpp

	assets.cpp
	#graphtest.cpp
	main.cpp
	shadergraph.cpp
	shadergraphmainwindow.cpp
	)

set(HEADERS
	core/guidhelper.h
	core/materialhelper.h
	core/materialwriter.h
	core/pbrgraphevaluator.h
	#core/project.h
	core/texturemanager.h
	core/undoredo.h
	core/sockethelper.h
	dialogs/createnewdialog.h
	dialogs/searchdialog.h

	generator/shadercontext.h
	generator/shadergenerator.h

	graph/graphnode.h
	graph/graphnodescene.h
	graph/nodegraph.h
	graph/socket.h
	graph/sockets.h
	graph/socketconnection.h

	misc/QtAwesome.h
	misc/QtAwesomeAnim.h

	models/connectionmodel.h
	models/library.h
	models/libraryv1.h
	models/nodemodel.h
	models/properties.h
	models/socketmodel.h

	nodes/inputs.h
	nodes/math.h
	nodes/object.h
	nodes/pbrmasternode.h
	nodes/test.h
	nodes/texture.h
	nodes/utils.h
	nodes/vector.h
	nodes/vertex.h
	nodes/view.h
	nodes/generated.h

	propertywidgets/floatpropertywidget.h
    propertywidgets/vectorpropertywidget.h
    propertywidgets/intpropertywidget.h
    propertywidgets/texturepropertywidget.h
    propertywidgets/basepropertywidget.h
    propertywidgets/propertywidgetbase.h

	widgets/graphicsview.h
	widgets/listwidget.h
	widgets/materialsettingswidget.h
	widgets/propertylistwidget.h
	widgets/scenewidget.h
	widgets/shaderlistwidget.h
	widgets/shaderassetwidget.h
	widgets/treewidget.h

	assets.h
	#graphtest.h
	main.h
	shadergraph.h
	shadergraphmainwindow.h
	)

set(FORMS
        mainwindow.ui
		materialsettingswidget.ui
        propertylistwidget.ui)

Qt5_add_resources(QRCS
		images.qrc
		icons.qrc
		fonts.qrc)


set(LIBS IrisGL Qt5::Widgets)


foreach(p CMP0071) # 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
    if(POLICY ${p})
        cmake_policy(SET ${p} NEW)
    endif()
endforeach()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)


#add_library(shadergraph STATIC ${HEADERS} ${CPP} ${FORMS} ${QRCS})
#target_link_libraries(shadergraph Qt5::Widgets Qt5::Sql)

if (EFFECT_BUILD_AS_LIB)

	add_definitions(-DBUILD_AS_LIB)

	add_library(shadergraph STATIC ${HEADERS} ${CPP} ${FORMS} ${QRCS})
	target_link_libraries(shadergraph Qt5::Widgets)
else(EFFECT_BUILD_AS_LIB)

	#qt5_wrap_cpp(HEADERS_moc ${HEADERS})
	#qt5_wrap_ui(UI_moc ${FORMS})

	set(CPP main.cpp ${CPP})
	add_executable(shadergraph ${HEADERS} ${CPP} ${FORMS} ${QRCS})
	include_directories(shadergraph "irisgl/include")
	target_link_libraries(shadergraph ${LIBS})

	if (WIN32)
	    get_target_property(QT5_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
	    get_filename_component(QT5_WINDEPLOYQT_EXECUTABLE ${QT5_QMAKE_EXECUTABLE} PATH)
	    set(QT5_WINDEPLOYQT_EXECUTABLE "${QT5_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")


	    add_custom_command(TARGET shadergraph POST_BUILD
	        COMMAND ${QT5_WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:shadergraph>)
	endif(WIN32)

	# Copy Jahshaka data folders after a successful build
	set(DataDirs assets)
	foreach(dir ${DataDirs})
		if (APPLE)
			add_custom_command(
				TARGET shadergraph POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_directory
						${PROJECT_SOURCE_DIR}/${dir}
						${PROJECT_BINARY_DIR}/${APP_OUTPUT_NAME}.app/Contents/MacOS/${dir})
		else()
			add_custom_command(
				TARGET shadergraph POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_directory
						${PROJECT_SOURCE_DIR}/${dir}
						${PROJECT_BINARY_DIR}/${dir})
		endif()
	endforeach()

endif(EFFECT_BUILD_AS_LIB)
