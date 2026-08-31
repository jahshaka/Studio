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
	src/modules/materials/core/bakeprogram.cpp
	src/modules/materials/core/materialhelper.cpp
	src/modules/materials/core/materialwriter.cpp
	src/modules/materials/core/pbrgraphevaluator.cpp
	#src/modules/materials/core/project.cpp
	src/modules/materials/core/texturemanager.cpp
	src/modules/materials/core/undoredo.cpp
	src/modules/materials/core/sockethelper.cpp

	src/modules/materials/dialogs/createnewdialog.cpp
	src/modules/materials/dialogs/searchdialog.cpp

	src/modules/materials/generator/shadercontext.cpp
	src/modules/materials/generator/shadergenerator.cpp

	src/modules/materials/graph/graphnode.cpp
	src/modules/materials/graph/graphnodescene.cpp
	src/modules/materials/graph/nodegraph.cpp
	src/modules/materials/graph/socket.cpp
	src/modules/materials/graph/socketconnection.cpp

	#src/modules/materials/misc/QtAwesome.cpp
	#src/modules/materials/misc/QtAwesomeAnim.cpp

	src/modules/materials/models/connectionmodel.cpp
	src/modules/materials/models/library.cpp
	src/modules/materials/models/libraryv1.cpp
	src/modules/materials/models/nodemodel.cpp
	src/modules/materials/models/properties.cpp
	src/modules/materials/models/socketmodel.cpp

	src/modules/materials/nodes/inputs.cpp
	src/modules/materials/nodes/math.cpp
	src/modules/materials/nodes/object.cpp
	src/modules/materials/nodes/pbrmasternode.cpp
	src/modules/materials/nodes/test.cpp
	src/modules/materials/nodes/texture.cpp
	src/modules/materials/nodes/utils.cpp
	src/modules/materials/nodes/vector.cpp
	src/modules/materials/nodes/view.cpp

	src/modules/materials/propertywidgets/floatpropertywidget.cpp
    src/modules/materials/propertywidgets/vectorpropertywidget.cpp
    src/modules/materials/propertywidgets/intpropertywidget.cpp
    src/modules/materials/propertywidgets/texturepropertywidget.cpp
    src/modules/materials/propertywidgets/basepropertywidget.cpp
    src/modules/materials/propertywidgets/propertywidgetbase.cpp

	src/modules/materials/widgets/graphicsview.cpp
	src/modules/materials/widgets/listwidget.cpp
	src/modules/materials/widgets/materialsettingswidget.cpp
	src/modules/materials/widgets/propertylistwidget.cpp
	src/modules/materials/widgets/shaderlistwidget.cpp
	src/modules/materials/widgets/shaderassetwidget.cpp
	src/modules/materials/widgets/treewidget.cpp

	#graphtest.cpp
	src/modules/materials/shadergraph.cpp
	src/modules/materials/effectspage.cpp
	)

set(HEADERS
	${HEADERS}
	src/modules/materials/core/bakeprogram.h
	src/modules/materials/core/guidhelper.h
	src/modules/materials/core/materialhelper.h
	src/modules/materials/core/materialwriter.h
	src/modules/materials/core/pbrgraphevaluator.h
	#src/modules/materials/core/project.h
	src/modules/materials/core/texturemanager.h
	src/modules/materials/core/undoredo.h
	src/modules/materials/core/sockethelper.h
	src/modules/materials/dialogs/createnewdialog.h
	src/modules/materials/dialogs/searchdialog.h

	src/modules/materials/generator/shadercontext.h
	src/modules/materials/generator/shadergenerator.h

	src/modules/materials/graph/graphnode.h
	src/modules/materials/graph/graphnodescene.h
	src/modules/materials/graph/nodegraph.h
	src/modules/materials/graph/socket.h
	src/modules/materials/graph/sockets.h
	src/modules/materials/graph/socketconnection.h

	#src/modules/materials/misc/QtAwesome.h
	#src/modules/materials/misc/QtAwesomeAnim.h

	src/modules/materials/models/connectionmodel.h
	src/modules/materials/models/library.h
	src/modules/materials/models/libraryv1.h
	src/modules/materials/models/nodemodel.h
	src/modules/materials/models/properties.h
	src/modules/materials/models/socketmodel.h

	src/modules/materials/nodes/inputs.h
	src/modules/materials/nodes/math.h
	src/modules/materials/nodes/object.h
	src/modules/materials/nodes/pbrmasternode.h
	src/modules/materials/nodes/test.h
	src/modules/materials/nodes/texture.h
	src/modules/materials/nodes/utils.h
	src/modules/materials/nodes/vector.h
	src/modules/materials/nodes/vertex.h
	src/modules/materials/nodes/view.h
	src/modules/materials/nodes/generated.h

	src/modules/materials/propertywidgets/floatpropertywidget.h
    src/modules/materials/propertywidgets/vectorpropertywidget.h
    src/modules/materials/propertywidgets/intpropertywidget.h
    src/modules/materials/propertywidgets/texturepropertywidget.h
    src/modules/materials/propertywidgets/basepropertywidget.h
    src/modules/materials/propertywidgets/propertywidgetbase.h

	src/modules/materials/widgets/graphicsview.h
	src/modules/materials/widgets/listwidget.h
	src/modules/materials/widgets/materialsettingswidget.h
	src/modules/materials/widgets/propertylistwidget.h
	src/modules/materials/widgets/shaderlistwidget.h
	src/modules/materials/widgets/shaderassetwidget.h
	src/modules/materials/widgets/treewidget.h

	#graphtest.h
	src/modules/materials/shadergraph.h
	src/modules/materials/effectspage.h
	)



#Qt5_add_resources(QRCS
#		src/modules/materials/images.qrc
#		src/modules/materials/icons.qrc
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
		
