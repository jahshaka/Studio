# =====================================================
# Deploy target for Windows and Linux
# =====================================================

set(DEPLOY_STAMP "${CMAKE_BINARY_DIR}/deploy.stamp")

if (WIN32)
    find_program(QT6_WINDEPLOYQT_EXECUTABLE windeployqt)
    if(NOT QT6_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "windeployqt not found in PATH. Please make sure Qt/bin is in your system PATH.")
    endif()

    add_custom_command(
        OUTPUT "${DEPLOY_STAMP}"
        COMMAND "${QT6_WINDEPLOYQT_EXECUTABLE}"
                "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                --dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                -multimedia

        COMMAND ${CMAKE_COMMAND} -E touch "${DEPLOY_STAMP}"
        DEPENDS ${CMAKE_PROJECT_NAME}
        COMMENT "[Deploy] Running windeployqt..."
    )

elseif(UNIX)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            OUTPUT "${DEPLOY_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E touch "${DEPLOY_STAMP}"
            DEPENDS ${CMAKE_PROJECT_NAME}
            COMMENT "Nothing..."
        )
    else()
        #Determine Qt6 base directory from Qt6Core_DIR
        get_filename_component(QT6_BASE_DIR "${Qt6Core_DIR}/../../../" ABSOLUTE)

        add_custom_command(
            OUTPUT "${DEPLOY_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E echo "Deploying ${CMAKE_PROJECT_NAME} and dependencies..."
            COMMAND bash "${CMAKE_SOURCE_DIR}/deploy/data/linux/copy_linux_deps.sh"
                    "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"          # executable
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"             # destination
                    "${QT6_BASE_DIR}"                               # Qt6 base


            COMMAND ${CMAKE_COMMAND} -E copy
                    # Generator expressions track the real built names (Debug 'd'
                    # suffix, soname) so a Bullet bump or build type cannot silently
                    # break the deploy (audit §5.4.6: the old hardcoded .so.2.88
                    # paths missed Debug entirely).
                    "$<TARGET_FILE:zip>"
                    "$<TARGET_FILE:BulletDynamics>"
                    "$<TARGET_FILE:LinearMath>"
                    "$<TARGET_FILE:BulletCollision>"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"

            COMMAND ${CMAKE_COMMAND} -E touch "${DEPLOY_STAMP}"
            DEPENDS ${CMAKE_PROJECT_NAME}
            COMMENT "[Deploy] Copying all runtime dependencies after build"
        )
    endif()

else()
    message(WARNING "Deploy target not defined for this platform")
endif()

# Unified deploy target
add_custom_target(deploy_app ALL DEPENDS "${DEPLOY_STAMP}" COMMENT "[Deploy] deploy_app target")
