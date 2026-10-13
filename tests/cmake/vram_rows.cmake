# THE CARD'S SLOTS (TESTING-SIMPLE; DOCS/PROCESS.md). One ctest process at a time holds the card (scripts/gpu-exclusive.sh:
# spec.sh test / merge and sprint.sh ship hold it around their ctest); INSIDE that process ctest schedules every GPU row on
# the `vram` resource of this build's resource spec — a WIDTH PER CLASS, as constants (tests/support/test_boot.cmake):
# a fixture (CLASS engine|selftest) takes 1 of the 8 slots (8 run at once), an app or VR row or a pool 2 (4 at once), a
# measuring row all 8 (alone). A row measured heavier than its class says `TOKENS <k>` (its slots) at its site.
set(JAH_CARD_SLOTS 8)

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
    set(_n ${_k})
    if(_k STREQUAL "all" OR _n GREATER JAH_CARD_SLOTS)
        set(_n ${JAH_CARD_SLOTS})
    endif()
    set_property(TEST ${_row} PROPERTY RESOURCE_GROUPS "vram:${_n}")
endfunction()
