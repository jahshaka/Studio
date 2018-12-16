#project(ShaderGraph)

set (CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt5 REQUIRED COMPONENTS Widgets Core )
add_definitions(-DEFFECT_BUILD_AS_LIB )

#add_subdirectory(irisgl)
# set_target_properties(IrisGL PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# set_target_properties(assimp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(SRCS
	${SRCS}
	#main.cpp
	src/shadergraph/mainwindow.cpp
	#nodegraph.cpp
    #nodemodel.cpp
    src/shadergraph/scenewidget.cpp
    src/shadergraph/propertylistwidget.cpp
	src/shadergraph/materialsettingswidget.cpp
    src/shadergraph/propertywidgets/vectorpropertywidget.cpp
    src/shadergraph/propertywidgets/intpropertywidget.cpp
    src/shadergraph/propertywidgets/floatpropertywidget.cpp
    src/shadergraph/propertywidgets/texturepropertywidget.cpp
    src/shadergraph/propertywidgets/basepropertywidget.cpp
    src/shadergraph/propertywidgets/propertywidgetbase.cpp
    src/shadergraph/properties.cpp
    src/shadergraph/materialwriter.cpp
    src/shadergraph/texturemanager.cpp
	src/shadergraph/socketconnection.cpp
	src/shadergraph/socket.cpp
	src/shadergraph/graphnode.cpp
	src/shadergraph/graphnodescene.cpp
	src/shadergraph/shaderassetwidget.cpp
	src/shadergraph/core/materialhelper.cpp
    #graphtest.cpp
	# graph
	src/shadergraph/graph/nodegraph.cpp
	src/shadergraph/graph/nodemodel.cpp
	src/shadergraph/graph/connectionmodel.cpp
	src/shadergraph/graph/socketmodel.cpp
	src/shadergraph/graph/sockethelper.cpp
	src/shadergraph/graph/library.cpp
	src/shadergraph/graph/graphicsview.cpp

	# generator
	src/shadergraph/generator/shadergenerator.cpp
	src/shadergraph/generator/shadercontext.cpp
	src/shadergraph/listwidget.cpp
	src/shadergraph/shaderlistwidget.cpp
	# nodes
	src/shadergraph/nodes/test.cpp
	src/shadergraph/nodes/libraryv1.cpp
	src/shadergraph/nodes/math.cpp
	src/shadergraph/nodes/utils.cpp
	src/shadergraph/nodes/inputs.cpp
	src/shadergraph/nodes/object.cpp
	src/shadergraph/dialogs/createnewdialog.cpp
	src/shadergraph/dialogs/searchdialog.cpp
	src/shadergraph/nodes/vector.cpp
	src/shadergraph/nodes/texture.cpp
	)

set(HEADERS
	${HEADERS}
	src/shadergraph/mainwindow.h
        src/shadergraph/properties.h
	#nodegraph.h
        src/shadergraph/scenewidget.h
        src/shadergraph/propertylistwidget.h
        src/shadergraph/materialsettingswidget.h
        src/shadergraph/propertywidgets/floatpropertywidget.h
        src/shadergraph/propertywidgets/vectorpropertywidget.h
        src/shadergraph/propertywidgets/intpropertywidget.h
        src/shadergraph/propertywidgets/texturepropertywidget.h
        src/shadergraph/propertywidgets/basepropertywidget.h
        src/shadergraph/propertywidgets/propertywidgetbase.h
        src/shadergraph/materialwriter.h
		src/shadergraph/texturemanager.h
		src/shadergraph/socketconnection.h
		src/shadergraph/socket.h
		src/shadergraph/graphnode.h
		src/shadergraph/graphnodescene.h
		src/shadergraph/shaderassetwidget.h
		src/shadergraph/core/materialhelper.h
        #graphtest.h
		#graph
		src/shadergraph/graph/nodegraph.h
		src/shadergraph/graph/nodemodel.h
		src/shadergraph/graph/connectionmodel.h
		src/shadergraph/graph/socketmodel.h
		src/shadergraph/graph/sockethelper.h
		src/shadergraph/graph/sockets.h
		src/shadergraph/graph/library.h
		src/shadergraph/graph/graphicsview.h
		# generator
		src/shadergraph/generator/shadergenerator.h
		src/shadergraph/generator/shadercontext.h
		src/shadergraph/listwidget.h
		src/shadergraph/shaderlistwidget.h
		# nodes
		src/shadergraph/nodes/test.h
		src/shadergraph/nodes/libraryv1.h
		src/shadergraph/nodes/math.h
		src/shadergraph/nodes/utils.h
		src/shadergraph/nodes/inputs.h
		src/shadergraph/nodes/object.h
		src/shadergraph/nodes/vector.h
		src/shadergraph/nodes/texture.h

		src/shadergraph/dialogs/createnewdialog.h
		src/shadergraph/dialogs/searchdialog.h
	)



#Qt5_add_resources(QRCS
#		src/shadergraph/images.qrc
#		src/shadergraph/icons.qrc
#		)

message("Shader graph included")
		