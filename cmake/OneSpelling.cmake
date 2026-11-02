# THE ONE SPELLING (URGENT #129): ~/Developer is a symlink to /mnt/work/Developer. A configured tree re-configured under the
# other spelling of the same directory re-spells every compile command and ninja rebuilds all of it; refuse that, change nothing.
if(DEFINED CACHE{JAH_CONFIGURED_SOURCE_DIR} AND NOT JAH_CONFIGURED_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    file(REAL_PATH "${JAH_CONFIGURED_SOURCE_DIR}" _jah_was)
    file(REAL_PATH "${CMAKE_SOURCE_DIR}" _jah_now)
    if(_jah_was STREQUAL _jah_now)
        message(FATAL_ERROR "This build tree was configured as\n  ${JAH_CONFIGURED_SOURCE_DIR}\nand you are configuring it as\n"
                "  ${CMAKE_SOURCE_DIR}\n— the same directory under a second spelling (the ~/Developer symlink). Every compile "
                "command would change and the whole tree rebuild (URGENT #129). Run from the first spelling:\n"
                "  cd ${JAH_CONFIGURED_SOURCE_DIR} && cmake --build <build dir>")
    endif()
endif()
set(JAH_CONFIGURED_SOURCE_DIR "${CMAKE_SOURCE_DIR}" CACHE INTERNAL "the spelling this tree was configured under (URGENT #129)")
