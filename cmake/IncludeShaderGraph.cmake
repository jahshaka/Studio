#project(ShaderGraph)

set (CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core )
add_definitions(-DEFFECT_BUILD_AS_LIB )

#add_subdirectory(irisgl)
# set_target_properties(IrisGL PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# set_target_properties(assimp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(SRCS
	${SRCS}
	#main.cpp
	src/shadergraph/core/materialhelper.cpp
	src/shadergraph/core/materialwriter.cpp
	#src/shadergraph/core/project.cpp
	src/shadergraph/core/texturemanager.cpp
	src/shadergraph/core/undoredo.cpp
	src/shadergraph/core/sockethelper.cpp

	src/shadergraph/dialogs/createnewdialog.cpp
	src/shadergraph/dialogs/searchdialog.cpp

	src/shadergraph/generator/shadercontext.cpp
	src/shadergraph/generator/shadergenerator.cpp

	src/shadergraph/graph/graphnode.cpp
	src/shadergraph/graph/graphnodescene.cpp
	src/shadergraph/graph/nodegraph.cpp
	src/shadergraph/graph/socket.cpp
	src/shadergraph/graph/socketconnection.cpp

	#src/shadergraph/misc/QtAwesome.cpp
	#src/shadergraph/misc/QtAwesomeAnim.cpp

	src/shadergraph/models/connectionmodel.cpp
	src/shadergraph/models/library.cpp
	src/shadergraph/models/libraryv1.cpp
	src/shadergraph/models/nodemodel.cpp
	src/shadergraph/models/properties.cpp
	src/shadergraph/models/socketmodel.cpp

	src/shadergraph/nodes/inputs.cpp
	src/shadergraph/nodes/math.cpp
	src/shadergraph/nodes/object.cpp
	src/shadergraph/nodes/test.cpp
	src/shadergraph/nodes/texture.cpp
	src/shadergraph/nodes/utils.cpp
	src/shadergraph/nodes/vector.cpp
	src/shadergraph/nodes/view.cpp

	src/shadergraph/propertywidgets/floatpropertywidget.cpp
    src/shadergraph/propertywidgets/vectorpropertywidget.cpp
    src/shadergraph/propertywidgets/intpropertywidget.cpp
    src/shadergraph/propertywidgets/texturepropertywidget.cpp
    src/shadergraph/propertywidgets/basepropertywidget.cpp
    src/shadergraph/propertywidgets/propertywidgetbase.cpp

	src/shadergraph/widgets/graphicsview.cpp
	src/shadergraph/widgets/listwidget.cpp
	src/shadergraph/widgets/materialsettingswidget.cpp
	src/shadergraph/widgets/propertylistwidget.cpp
	src/shadergraph/widgets/scenewidget.cpp
	src/shadergraph/widgets/shaderlistwidget.cpp
	src/shadergraph/widgets/shaderassetwidget.cpp
	src/shadergraph/widgets/treewidget.cpp

	src/shadergraph/assets.cpp
	#graphtest.cpp
	src/shadergraph/shadergraph.cpp
	src/shadergraph/shadergraphmainwindow.cpp
	)

set(HEADERS
	${HEADERS}
	src/shadergraph/core/guidhelper.h
	src/shadergraph/core/materialhelper.h
	src/shadergraph/core/materialwriter.h
	#src/shadergraph/core/project.h
	src/shadergraph/core/texturemanager.h
	src/shadergraph/core/undoredo.h
	src/shadergraph/core/sockethelper.h
	src/shadergraph/dialogs/createnewdialog.h
	src/shadergraph/dialogs/searchdialog.h

	src/shadergraph/generator/shadercontext.h
	src/shadergraph/generator/shadergenerator.h

	src/shadergraph/graph/graphnode.h
	src/shadergraph/graph/graphnodescene.h
	src/shadergraph/graph/nodegraph.h
	src/shadergraph/graph/socket.h
	src/shadergraph/graph/sockets.h
	src/shadergraph/graph/socketconnection.h

	#src/shadergraph/misc/QtAwesome.h
	#src/shadergraph/misc/QtAwesomeAnim.h

	src/shadergraph/models/connectionmodel.h
	src/shadergraph/models/library.h
	src/shadergraph/models/libraryv1.h
	src/shadergraph/models/nodemodel.h
	src/shadergraph/models/properties.h
	src/shadergraph/models/socketmodel.h

	src/shadergraph/nodes/inputs.h
	src/shadergraph/nodes/math.h
	src/shadergraph/nodes/object.h
	src/shadergraph/nodes/test.h
	src/shadergraph/nodes/texture.h
	src/shadergraph/nodes/utils.h
	src/shadergraph/nodes/vector.h
	src/shadergraph/nodes/vertex.h
	src/shadergraph/nodes/view.h
	src/shadergraph/nodes/generated.h

	src/shadergraph/propertywidgets/floatpropertywidget.h
    src/shadergraph/propertywidgets/vectorpropertywidget.h
    src/shadergraph/propertywidgets/intpropertywidget.h
    src/shadergraph/propertywidgets/texturepropertywidget.h
    src/shadergraph/propertywidgets/basepropertywidget.h
    src/shadergraph/propertywidgets/propertywidgetbase.h

	src/shadergraph/widgets/graphicsview.h
	src/shadergraph/widgets/listwidget.h
	src/shadergraph/widgets/materialsettingswidget.h
	src/shadergraph/widgets/propertylistwidget.h
	src/shadergraph/widgets/scenewidget.h
	src/shadergraph/widgets/shaderlistwidget.h
	src/shadergraph/widgets/shaderassetwidget.h
	src/shadergraph/widgets/treewidget.h

	src/shadergraph/assets.h
	#graphtest.h
	src/shadergraph/shadergraph.h
	src/shadergraph/shadergraphmainwindow.h
	)



#Qt5_add_resources(QRCS
#		src/shadergraph/images.qrc
#		src/shadergraph/icons.qrc
#		)

# set(shaderSource ${CMAKE_CURRENT_SOURCE_DIR})
# message("${CMAKE_CURRENT_SOURCE_DIR}  shader graph source")

# macro(copy_dirs dirs)
# foreach(dir ${dirs})
#     # Replace / at the end of the path (copy dir content VS copy dir)
#     string(REGEX REPLACE "/+$" "" dirclean "${dir}")
#     message(STATUS "Copying resource ${dirclean}")
#     file(COPY ${dirclean} DESTINATION ${DestDir})
# endforeach()
# endmacro()

# set(dir assets)

# if (APPLE)
# 	add_custom_command(
# 		TARGET ${PROJECT_NAME} POST_BUILD
# 		COMMAND ${CMAKE_COMMAND} -E copy_directory
# 				${shaderSource}/${dir}
# 				${DestDir}/${APP_OUTPUT_NAME}.app/Contents/MacOS/${dir})
# else()
# 	add_custom_command(
# 		TARGET ${PROJECT_NAME} POST_BUILD
# 		COMMAND ${CMAKE_COMMAND} -E copy_directory
# 				${shaderSource}/${dir}
# 				${DestDir}/${dir})
# endif()
		