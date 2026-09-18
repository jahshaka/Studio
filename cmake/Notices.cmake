# Notices.cmake — THE THIRD-PARTY NOTICE RESOURCES (NOTICES-1, 2026-09-18).
#
# The manifest (app/notices.json) declares one entry per vendored component and
# NEVER carries a licence text. This turns each entry into a resource read out
# of the vendored tree itself, so the text in the shipped binary is the text in
# the tree by construction:
#
#   * `file`          -> the licence file is embedded as it stands;
#   * `file` + `lines`-> the notice lives in a comment inside a source or a
#                        provenance document, so the stated line range is
#                        EXTRACTED into the build dir and that is embedded
#                        (still read from the vendored file, never retyped);
#   * `optional: true`-> a component that may be absent from a checkout (an
#                        uninitialised submodule). A missing file is then a
#                        generated one-line note instead of a hard error.
#
# Anything else missing is a FATAL_ERROR: a notice surface that silently drops a
# component is worse than none, because it reads as complete.
#
# The generated .qrc lists the REAL FILE PATHS, so rcc re-runs when a vendored
# licence file changes (ninja tracks the qrc's entries) — and the manifest
# itself rides CMAKE_CONFIGURE_DEPENDS, so editing it re-runs this.

function(jah_generate_notices out_qrc)
    set(_manifest "${CMAKE_SOURCE_DIR}/app/notices.json")
    if(NOT EXISTS "${_manifest}")
        message(FATAL_ERROR "notices: ${_manifest} is missing")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_manifest}")

    file(READ "${_manifest}" _json)
    string(JSON _count LENGTH "${_json}" components)
    set(_gen "${CMAKE_BINARY_DIR}/generated/notices")
    file(MAKE_DIRECTORY "${_gen}")

    # The manifest travels WITH the texts: the runtime list (names, roles,
    # licences, homepages) is this same file, parsed from the resource, so there
    # is one declaration and no C++ copy of it.
    configure_file("${_manifest}" "${_gen}/notices.json" COPYONLY)
    set(_entries "    <file alias=\"notices.json\">${_gen}/notices.json</file>\n")

    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE 0 ${_last})
        string(JSON _id     GET "${_json}" components ${_i} id)
        string(JSON _path   GET "${_json}" components ${_i} path)
        string(JSON _file   GET "${_json}" components ${_i} file)
        string(JSON _opt ERROR_VARIABLE _e GET "${_json}" components ${_i} optional)
        if(NOT _opt)
            set(_opt "OFF")
        endif()
        string(JSON _lines ERROR_VARIABLE _le GET "${_json}" components ${_i} lines)

        set(_src "${CMAKE_SOURCE_DIR}/${_path}/${_file}")
        if(NOT EXISTS "${_src}")
            if(_opt)
                # Honest, and visible in the app: the component is declared but
                # this checkout does not carry it (an uninitialised submodule),
                # so the binary cannot have been built with it either.
                file(WRITE "${_gen}/${_id}.txt"
                     "This component is declared in the third-party manifest but is not present "
                     "in this checkout (${_path}/${_file} is missing), so this build does not "
                     "include it. Its licence is named in the list beside this text; the full "
                     "text ships with any build that does include the component.\n")
                string(APPEND _entries
                       "    <file alias=\"${_id}.txt\">${_gen}/${_id}.txt</file>\n")
                continue()
            endif()
            message(FATAL_ERROR
                    "notices: component '${_id}' names ${_path}/${_file}, which does not exist. "
                    "Fix the manifest (app/notices.json) or mark the entry optional.")
        endif()

        if(NOT _le)
            # An extracted range: read the vendored file, keep the stated lines.
            # `string(JSON ... GET)` hands back the array as JSON TEXT, so the
            # two ends are read with string(JSON) too rather than list(GET).
            string(JSON _first_line GET "${_lines}" 0)
            string(JSON _last_line  GET "${_lines}" 1)
            # THROUGH PYTHON, and that is not a preference: CMake's own
            # `file(STRINGS)` mangles exactly the text a licence is made of (it
            # splits on semicolons and drops non-ASCII), which turned an em dash
            # into a line break in the first cut of this file. Same interpreter
            # the source lints use.
            if(NOT Python3_EXECUTABLE)
                find_package(Python3 COMPONENTS Interpreter QUIET)
            endif()
            if(NOT Python3_EXECUTABLE)
                message(FATAL_ERROR
                        "notices: component '${_id}' needs a line range extracted from "
                        "${_path}/${_file} and no python3 was found. Either install one or give "
                        "the component a licence file of its own in the manifest.")
            endif()
            execute_process(
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/extract_notice.py"
                        "${_src}" "${_first_line}" "${_last_line}" "${_gen}/${_id}.txt"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
            if(NOT _rc EQUAL 0)
                message(FATAL_ERROR
                        "notices: extracting lines ${_first_line}-${_last_line} of "
                        "${_path}/${_file} for '${_id}' failed: ${_err}")
            endif()
            # The EXTRACTION's input is a configure dependency: the range is
            # read here, so a change to the vendored file must re-run this.
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_src}")
            string(APPEND _entries
                   "    <file alias=\"${_id}.txt\">${_gen}/${_id}.txt</file>\n")
        else()
            # The licence file itself, by path — rcc reads it at build time and
            # ninja re-runs when it changes.
            string(APPEND _entries "    <file alias=\"${_id}.txt\">${_src}</file>\n")
        endif()
    endforeach()

    set(_qrc "${CMAKE_BINARY_DIR}/generated/notices.qrc")
    file(WRITE "${_qrc}"
         "<!DOCTYPE RCC><RCC version=\"1.0\">\n  <qresource prefix=\"/notices\">\n"
         "${_entries}"
         "  </qresource>\n</RCC>\n")
    message(STATUS "notices: ${_count} third-party component(s) -> ${_qrc}")
    set(${out_qrc} "${_qrc}" PARENT_SCOPE)
endfunction()
