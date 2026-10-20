# tests/avatar — THE AVATAR FIXTURES ONLY (TEST-CUT-1, 2026-10-20). The avatar suites (avatar.document, .preview, .locomotion,
# .movement, .possession, .assets, .locomotion_mirror) left the tree for archive/tests/avatar/ (the tag tests-before-cut-1
# holds them); what stays is the fixture two CORE rows read: avatar.responsive (tests/avatarasync) and scripting.e2e.one_asset
# (tests/scripting) spawn the default avatar from beta_mannequin.dae. Ship 3 (8d2ca389b) went red on both in a fresh tree
# because this generator went with the suites while main's build-linux still held the file it had made.
#
# beta_mannequin.dae — the DEFAULT AVATAR fixture: Mixamo's Beta character (full standard Mixamo skeleton, bare joint
# names) with its baked dance clip stripped so the body is neutral. GENERATED here rather than checked in: the ~5 MB of
# bytes already ship inside scenes/"Skeletal Animation.zip", and fixtures/make_beta_mannequin.py derives the clipless
# .dae from that archive. The consumers name it as ${CMAKE_BINARY_DIR}/tests/avatar/fixtures/beta_mannequin.dae.
set(JAHSHAKA_MANNEQUIN_DAE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/beta_mannequin.dae"
    CACHE INTERNAL "generated animation-free Mixamo Beta avatar fixture")
add_custom_command(OUTPUT "${JAHSHAKA_MANNEQUIN_DAE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_beta_mannequin.py"
            "${CMAKE_SOURCE_DIR}" "${JAHSHAKA_MANNEQUIN_DAE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_beta_mannequin.py"
            "${CMAKE_SOURCE_DIR}/scenes/Skeletal Animation.zip"
    COMMENT "Deriving beta_mannequin.dae (animation-free Mixamo Beta) from the sample archive"
    VERBATIM)
add_custom_target(beta_mannequin ALL DEPENDS "${JAHSHAKA_MANNEQUIN_DAE}")
