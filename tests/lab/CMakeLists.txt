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
