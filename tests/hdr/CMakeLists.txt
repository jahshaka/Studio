# THE HDR AUTO-EXPOSURE'S STABILITY (lane HDR-1, fork feab041c6 (was 0042)) — headless,
# framework-free, links JahshakaEngine ONLY, same runtime requirements as
# tests/engine and tests/ssr (a reachable DISPLAY, a Vulkan driver or lavapipe).
#
# It asserts on View::measuredExposureScale() — the number the tonemapper
# samples, read back off the chain's own 1x1 adaptation history — rather than on
# pixels, because the subject is the GRADE and a picture only shows it
# indirectly. See the file header for the defect and the measurements.
function(jahshaka_hdr_test target source)
    add_executable(${target} ${source})
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    add_dependencies(${target} JahshakaEngineMedia)
endfunction()

jahshaka_hdr_test(test_hdr_drag test_hdr_drag.cpp)
target_link_libraries(test_hdr_drag PRIVATE JahshakaEngine)
jah_gpu_row(hdr.drag_stable CLASS engine COMMAND test_hdr_drag
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(hdr.drag_stable PROPERTIES TIMEOUT 120 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_hdr_test(test_hdr_drag_asan test_hdr_drag.cpp)
    target_link_libraries(test_hdr_drag_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_hdr_drag_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_hdr_drag_asan PRIVATE -fsanitize=address)
    jah_gpu_row(hdr.drag_stable_asan CLASS engine COMMAND test_hdr_drag_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine and tests/ssr: the only leaks LSan finds here
    # are libdbus-1 allocations reached from the Vulkan loader's device-select layer.
    set_tests_properties(hdr.drag_stable_asan PROPERTIES TIMEOUT 180 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# THE HISTOGRAM METER'S ARITHMETIC (EXPOSURE-2). The companion of the suite
# above: that one asserts the grade is STEADY, this one asserts it is RIGHT, in
# absolute terms, against numbers computed on paper from a frame of known
# radiance. See the file header.
jahshaka_hdr_test(test_hdr_meter test_hdr_meter.cpp)
target_link_libraries(test_hdr_meter PRIVATE JahshakaEngine)
jah_gpu_row(hdr.meter CLASS engine COMMAND test_hdr_meter
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(hdr.meter PROPERTIES TIMEOUT 180 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_hdr_test(test_hdr_meter_asan test_hdr_meter.cpp)
    target_link_libraries(test_hdr_meter_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_hdr_meter_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_hdr_meter_asan PRIVATE -fsanitize=address)
    jah_gpu_row(hdr.meter_asan CLASS engine COMMAND test_hdr_meter_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(hdr.meter_asan PROPERTIES TIMEOUT 180 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# THE GRADE'S 8-BIT WRITE IS DITHERED (lane DITHER-1, fork feab041c6 (was 0079)). A
# gradient fixture under one point light, rendered twice in ONE process — with
# the dither and without it, through PostFxDesc::ditherOff — so every arm is
# measured against the same binary's undithered picture and the discrimination
# is real. See the file header for the defect (the owner's "rippling in the
# ground plane") and for what each arm would catch; the last one is not about
# the dither at all but about the LDR buffer SMAA works on, which was losing 73
# of the 256 display codes on its own.
jahshaka_hdr_test(test_hdr_dither test_hdr_dither.cpp)
target_link_libraries(test_hdr_dither PRIVATE JahshakaEngine)
jah_gpu_row(hdr.dither CLASS engine COMMAND test_hdr_dither
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(hdr.dither PROPERTIES TIMEOUT 120 LABELS "engine")

# THE BLOOM COMPOSITE TAKES AN AMOUNT (lane BLOOM-AMOUNT-1, fork feab041c6 (was 0082)).
# One bright emissive square on black, rendered four ways in ONE process —
# bloom off, and bloom on at 0, 1 and 2 — so the dial is measured as the halo's
# summed excess over the bloom-off picture rather than against another run. The
# file header says what each arm would catch; the two that carry the design are
# D (the ratio, which is what a scale applied inside the UNORM blur ladder or
# ahead of the composite's fromSRGB square would fail) and F (the workspace
# generation, which is what makes it a dial a person can drag).
jahshaka_hdr_test(test_hdr_bloom_amount test_hdr_bloom_amount.cpp)
target_link_libraries(test_hdr_bloom_amount PRIVATE JahshakaEngine)
jah_gpu_row(hdr.bloom_amount CLASS engine COMMAND test_hdr_bloom_amount
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(hdr.bloom_amount PROPERTIES TIMEOUT 120 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_hdr_test(test_hdr_bloom_amount_asan test_hdr_bloom_amount.cpp)
    target_link_libraries(test_hdr_bloom_amount_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_hdr_bloom_amount_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_hdr_bloom_amount_asan PRIVATE -fsanitize=address)
    jah_gpu_row(hdr.bloom_amount_asan CLASS engine COMMAND test_hdr_bloom_amount_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(hdr.bloom_amount_asan PROPERTIES TIMEOUT 120 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

if(JAHSHAKA_ASAN)
    jahshaka_hdr_test(test_hdr_dither_asan test_hdr_dither.cpp)
    target_link_libraries(test_hdr_dither_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_hdr_dither_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_hdr_dither_asan PRIVATE -fsanitize=address)
    jah_gpu_row(hdr.dither_asan CLASS engine COMMAND test_hdr_dither_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(hdr.dither_asan PROPERTIES TIMEOUT 120 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# THE BLOOM IS ISOTROPIC (BLOOM-1). One point light's round hot spot on a matte
# wall at 960x540, bloomed and at amount 0 in ONE process: the halo's half-width
# at half its peak across and down agree within 10 %, and the hot spot's row
# carries no excess at the frame's edges (the old fixed 256x256 ladder blurred 65
# taps across and 11 down: a full-width band). See the file header.
jahshaka_hdr_test(test_hdr_bloom_isotropic test_hdr_bloom_isotropic.cpp)
target_link_libraries(test_hdr_bloom_isotropic PRIVATE JahshakaEngine)
jah_gpu_row(hdr.bloom_isotropic CLASS engine COMMAND test_hdr_bloom_isotropic
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(hdr.bloom_isotropic PROPERTIES TIMEOUT 120 LABELS "engine")

# EXACTLY ONE sRGB ENCODE ON EVERY DISPLAY PATH (SRGB-ENCODE-1). A flat emissive
# plane at a ramp of known radiances, read back four ways in ONE process: the
# Plain instrument (linear, unchanged), the ungraded display picture in the
# passthrough and the post shape (the exact sRGB OETF), and the graded picture
# (the OETF of the film curve; the grey card at 118). See the file header.
jahshaka_hdr_test(test_hdr_display_encode test_hdr_display_encode.cpp)
target_link_libraries(test_hdr_display_encode PRIVATE JahshakaEngine)
jah_gpu_row(hdr.display_encode CLASS engine COMMAND test_hdr_display_encode
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(hdr.display_encode PROPERTIES TIMEOUT 120 LABELS "engine")

# hdr.bloom_showroom — NO FULL-WIDTH BAND IN THE PRODUCT (BLOOM-1, the app half of
# hdr.bloom_isotropic): Showroom 2 from its own camera, the editor's "scene" shot with
# bloom and at bloomAmount 0; every row's two end pixels agree within 2 codes unless a
# pixel bright enough to bloom lies within a halo's reach of them. An app run on the
# rig's display; a FRESH home per run (the sample is imported).
set(JAH_BLOOM_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file(scripts/bloom_showroom.js.in ${CMAKE_CURRENT_BINARY_DIR}/bloom_showroom.js @ONLY)
set(_bloom_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-bloom_showroom)
jah_fresh_home_fixture(hdr.bloom_showroom.fresh_home bloomShowroomHome "${_bloom_home}" LABELS "scripting")
jah_gpu_row(hdr.bloom_showroom CLASS app
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/bloom_showroom.js
         WORKING_DIRECTORY ${_bloom_home}/run)
set_tests_properties(hdr.bloom_showroom PROPERTIES
    FIXTURES_REQUIRED bloomShowroomHome
    TIMEOUT 300 LABELS "scripting"
    ENVIRONMENT "HOME=${_bloom_home};JAHSHAKA_DATA_ROOT=${_bloom_home}/.local/share/Jahshaka")
