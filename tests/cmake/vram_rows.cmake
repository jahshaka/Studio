# THE CARD'S SLOTS (TESTING-SIMPLE; DOCS/PROCESS.md). One ctest process at a time holds the card (scripts/gpu-exclusive.sh:
# spec.sh test / merge and sprint.sh ship hold it around their ctest); INSIDE that process ctest schedules every GPU row on
# the `vram` resource of this build's resource spec — a WIDTH PER CLASS, as constants (tests/support/test_boot.cmake):
# EVERY GPU row takes all 8 slots, so GPU rows run one at a time (urgent #44: a row's 60 s is its own cost, never its
# neighbours'); a row that boots no Vulkan takes none and overlaps them.
set(JAH_CARD_SLOTS 8)
# ...EXCEPT THE ROWS MEASURED TO SHARE IT (TEST-STREAMLINE-1 block C, the lead's per-row ruling, 2026-11-03): the rows of
# tests/support/card-width-2-rows.txt take HALF the slots, so two of them hold the card at once (block C: width 2's total
# 419 s against width 1's 825 s on its 141 rows); every other GPU row keeps all eight and runs alone.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../support/card-width-2-rows.txt" JAH_CARD_WIDTH2_ROWS REGEX "^[^#]")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../support/card-width-2-rows.txt")

# The build's ctest resource spec: one `vram` resource with the card's slots (ctest reads CTEST_RESOURCE_SPEC_FILE).
macro(jah_vram_resource_spec)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vram-resources.json"
         "{\"version\": {\"major\": 1, \"minor\": 0}, \"local\": [{\"vram\": [{\"id\": \"card\", \"slots\": ${JAH_CARD_SLOTS}}]}]}\n")
    set(CTEST_RESOURCE_SPEC_FILE "${CMAKE_CURRENT_BINARY_DIR}/vram-resources.json")
endmacro()

# A GPU row's registration: its class (the test property JAH_CLASS, read by the lints and the selector) and its slots.
function(_jah_vram_register _row _class _k)
    set_property(GLOBAL APPEND PROPERTY JAH_GPU_ROWS "${_row}|${_class}|${_k}")
    set_property(TEST ${_row} PROPERTY JAH_CLASS ${_class})
    if(NOT _class STREQUAL "engine")   # the shipped seed off (URGENT #143, below): an engine binary reads no seed
        cmake_language(EVAL CODE "cmake_language(DEFER CALL _jah_seed_off_apply [[${_row}]])")
    endif()
    set(_n ${_k})
    if(_k STREQUAL "all" OR _n GREATER JAH_CARD_SLOTS)
        set(_n ${JAH_CARD_SLOTS})
    endif()
    if(_n EQUAL JAH_CARD_SLOTS AND _row IN_LIST JAH_CARD_WIDTH2_ROWS)
        math(EXPR _n "${JAH_CARD_SLOTS} / 2")
    endif()
    set_property(TEST ${_row} PROPERTY RESOURCE_GROUPS "vram:${_n}")
endfunction()

# THE SHIPPED SEED IS OFF IN THE HARNESS (URGENT #143, 2026-11-02). A tree's <bin>/shadercache-seed (the ship tree after its
# seed step; COLD-BOOT-1's make-seed.sh) is served by EngineHost to EVERY data root without a microcode layer at the build's
# key: every cold test home would boot on the seed's SPIR-V and never run the build's own compile (only shadercache.app
# opted out, in its C++). So every GPU row that boots the app's EngineHost (class app, selftest, vr) carries
# JAHSHAKA_SHADER_SEED=set:none, appended at the end of its directory (after the site's own set_tests_properties, which
# would replace a list set now) UNLESS THE ROW STATES ITS OWN: a JAHSHAKA_SHADER_SEED entry in its ENVIRONMENT or
# ENVIRONMENT_MODIFICATION. A seed row opts back in with one line,
#     set_tests_properties(<row> PROPERTIES ENVIRONMENT_MODIFICATION "JAHSHAKA_SHADER_SEED=unset:")
# (the build's own seed resolves beside the binary). The warm-up's boots export the same none (scripts/warm-app-cache.sh);
# source.testing_rules R15 reads the registered set.
function(_jah_seed_off_apply _row)
    get_property(_env TEST ${_row} PROPERTY ENVIRONMENT)
    get_property(_mod TEST ${_row} PROPERTY ENVIRONMENT_MODIFICATION)
    foreach(_e IN LISTS _env _mod)
        if(_e MATCHES "^JAHSHAKA_SHADER_SEED=")
            return()
        endif()
    endforeach()
    set_property(TEST ${_row} APPEND PROPERTY ENVIRONMENT_MODIFICATION "JAHSHAKA_SHADER_SEED=set:none")
endfunction()
