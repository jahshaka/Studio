# jahlab's rows (LAB-1; SPECS/LAB_1_SPEC.md stage 1). The lab is a binary of its own (src/lab), so its rows run IT:
# each is a short driver script over the real jahlab, on the rig's display (Ogre-Next's Vulkan connects to $DISPLAY
# at boot even for an offscreen view), booting warm on the fixtures' shared shader cache (CLASS engine: the harness
# hands JAHSHAKA_SHADER_CACHE_DIR over and jahlab honours it).
if(NOT TARGET jahlab)
    return()
endif()

# lab.boots_and_shoots — `jahlab --headless --script` on the kit-bashed box set (tests/meshbake's kitBash() as an OBJ,
# kitbash_obj.py): loads, bakes into the lab's data root, Triangles view, 30 frames, a PNG, the status JSON (the cut
# drew, its pixels are on screen); a second run of the same script hits the bake cache (no parse, no bake) and cuts the
# same. Two runs of 41 frames.
jah_gpu_row(lab.boots_and_shoots CLASS engine FRAMES 82
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/boots_and_shoots.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/boots_and_shoots
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.boots_and_shoots PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.commands_are_one_parser — a KEY (xdotool F2 x 2 on the window), a --script line and a stdin line each put the
# scene on the Atom Levels view: the lab's status says levels three times, and the engine's presented frame (the frame
# capture) is one picture byte for byte, unlike the `view off` frame between them. ~45 frames on the window.
# (no xdotool on the box = the row FAILS by name — never silently absent)
jah_gpu_row(lab.commands_are_one_parser CLASS engine FRAMES 45
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/commands_are_one_parser.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/commands_are_one_parser
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.commands_are_one_parser PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.envs_build (stage 2) — `jahlab --env-check` per environment, the eight in parallel (envs_build.sh): each builds
# (its meshes through the bake's stages) under 5 s, and rebuilds AND re-bakes deterministically: the same seed gives
# the same triangle count, scene hash and bake hash (the bake pool's threaded output), the next seed another hash. CPU.
add_test(NAME lab.envs_build COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/envs_build.sh $<TARGET_FILE:jahlab>
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.envs_build PROPERTIES TIMEOUT 60 LABELS "lab" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(lab.envs_build)

# lab.env_expectation_printed (stage 2) — `jahlab --env-list` prints every environment's law and its closed-form
# expectation at each dolly stop (22 / 88 / 177 / 354 m), every value a finite number. CPU only.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME lab.env_expectation_printed
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/env_expectation_printed.py $<TARGET_FILE:jahlab>
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.env_expectation_printed PROPERTIES TIMEOUT 60 LABELS "lab" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(lab.env_expectation_printed)

# lab.dolly_matches_probe (stage 3) — scripts/lab/dolly.sh on the kit-bashed box set at two stops: the dolly's table
# equals the engine's AtomDrawStatus at each stop (cut triangles, clusters, occluded objects) and the cut probe's drawn
# triangles equal that cut, +- 0. On the rig's display; the dolly takes the card's lock (re-entrant under a holder).
jah_gpu_row(lab.dolly_matches_probe CLASS engine FRAMES 30
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/dolly_matches_probe.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/dolly_matches_probe
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.dolly_matches_probe PROPERTIES TIMEOUT 60 LABELS "lab")
