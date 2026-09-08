# nogrouplesstexture.cmake — the SEGV's precondition, asserted on a run's logs.
#
# A texture created with no resource group and without the ManualTexture flag is,
# to Ogre, a file texture with nowhere to load from: the streaming and multiload
# workers build a LoadRequest with a null archive AND a null loading listener,
# log "ERROR: Did you call createTexture with a valid resourceGroup?" and then
# dereference the null listener (OgreTextureGpuManager.cpp:2745/2864). That line
# preceded both the 2026-09-08 SIGSEGV in
# _updateTextureMultiLoadWorkerThread (SPECS/OGRE_UPSTREAM_ISSUES.md) and the
# import hang, so its ABSENCE is the cheapest possible regression gate: one grep
# over the logs a suite just wrote.
#
# It is deliberately a SEPARATE ctest entry rather than a line inside the
# scripts: the message is written by Ogre's LogManager from a WORKER thread, so
# no script can see it, and a suite that crashed on it would never reach its own
# assertions anyway.
if(NOT DEFINED LOG_DIR)
    message(FATAL_ERROR "nogrouplesstexture.cmake: -DLOG_DIR=<path> is required")
endif()
file(GLOB _logs ${LOG_DIR}/*.log)
if(NOT _logs)
    message(FATAL_ERROR "nogrouplesstexture.cmake: no logs in ${LOG_DIR} — did the run happen?")
endif()
foreach(_log ${_logs})
    file(STRINGS ${_log} _hits REGEX "valid resourceGroup")
    if(_hits)
        message(FATAL_ERROR
            "${_log} contains the group-less texture error — a texture reached the "
            "streaming worker with no resource group and no ManualTexture flag: ${_hits}")
    endif()
endforeach()
message(STATUS "no group-less texture requests in ${LOG_DIR}")
