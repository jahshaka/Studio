if(APPLE)

	get_target_property(QT5_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
	get_filename_component(MACDEPLOYQT ${QT5_QMAKE_EXECUTABLE} PATH)
	set(MACDEPLOYQT "${MACDEPLOYQT}/macdeployqt")

        set(ICON ${CMAKE_CURRENT_SOURCE_DIR}/app/icons/icon.icns)

        set_source_files_properties(${ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

        # using fileicon from a npm package to set icon for app
        # https://github.com/mklement0/fileicon#installation
        add_custom_command(
                TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                COMMAND /usr/local/bin/fileicon set ${DestDir}/${PROJECT_NAME}.app ${ICON})

	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND ${MACDEPLOYQT} ${DestDir}/${PROJECT_NAME}.app -dmg	)

	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND hdiutil convert ${DestDir}/${PROJECT_NAME}.dmg -format UDRW -o ${DestDir}/${PROJECT_NAME}s.dmg	)

	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E remove ${DestDir}/${PROJECT_NAME}.dmg )

	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E rename ${DestDir}/${PROJECT_NAME}s.dmg ${DestDir}/${PROJECT_NAME}.dmg )

	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND hdiutil attach ${DestDir}/${PROJECT_NAME}.dmg )

	add_custom_command(
		TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND /usr/sbin/diskutil rename ${DestDir}/${PROJECT_NAME} ${CMAKE_PROJECT_NAME})

            # using fileicon from a npm package to set icon for dmg
            # https://github.com/mklement0/fileicon#installation
            add_custom_command(
                    TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                    COMMAND /usr/local/bin/fileicon set ${DestDir}/${PROJECT_NAME}.dmg ${ICON})

endif()
