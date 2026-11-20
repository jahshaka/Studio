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
# A FIRST CONFIGURE UNDER THE LINK's TARGET IS REFUSED TOO (WORKFLOW-STREAMLINE-1 6.5; §2552, §3949): a fresh build dir
# configured as /mnt/work/... - the target of the ~/Developer link - compiles every file outside ccache's base_dir (the
# whole cache missed) and spells every command the other way, so the tree's next configure under ~/Developer rebuilds it
# all. When the link IS a symlink and CMAKE_SOURCE_DIR or CMAKE_BINARY_DIR lies under its real path (not under the link
# itself), stop and name the ~/Developer spelling. The link is $ENV{HOME}/Developer; THE ONE OVERRIDE is the environment
# variable JAH_ONE_SPELLING_LINK - another link path, or `none` (no check). A box where ~/Developer is a real directory
# (or absent: a row's scratch HOME) has no second spelling, and nothing triggers. An environment variable, not a cache
# one: nothing a record keys on carries a path (URGENT #154).
set(_jah_link "$ENV{JAH_ONE_SPELLING_LINK}")
if(_jah_link STREQUAL "")
    set(_jah_link "$ENV{HOME}/Developer")
endif()
if(NOT _jah_link STREQUAL "none" AND IS_SYMLINK "${_jah_link}")
    file(REAL_PATH "${_jah_link}" _jah_target)
    string(LENGTH "${_jah_target}" _jah_n)
    foreach(_jah_dir CMAKE_SOURCE_DIR CMAKE_BINARY_DIR)
        set(_jah_p "${${_jah_dir}}")
        string(FIND "${_jah_p}/" "${_jah_link}/" _jah_at_link)
        string(FIND "${_jah_p}/" "${_jah_target}/" _jah_at_target)
        if(NOT _jah_at_link EQUAL 0 AND _jah_at_target EQUAL 0)
            string(SUBSTRING "${_jah_p}" ${_jah_n} -1 _jah_rest)
            message(FATAL_ERROR "${_jah_dir} is\n  ${_jah_p}\n— under ${_jah_target}, the target of the link ${_jah_link}. "
                    "A configure there compiles outside the shared ccache and spells every command the other way "
                    "(CODING_RULES §4, the ONE spelling). Configure under the link:\n  ${_jah_link}${_jah_rest}\n"
                    "(a box with another layout: JAH_ONE_SPELLING_LINK=<the link> or none, in the environment)")
        endif()
    endforeach()
endif()
set(JAH_CONFIGURED_SOURCE_DIR "${CMAKE_SOURCE_DIR}" CACHE INTERNAL "the spelling this tree was configured under (URGENT #129)")
