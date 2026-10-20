# tests/avatar — THE AVATAR FIXTURES ONLY (TEST-CUT-1, 2026-10-20). The avatar suites (avatar.document, .preview, .locomotion,
# .movement, .possession, .assets, .locomotion_mirror) left the tree for archive/tests/avatar/ (the tag tests-before-cut-1
# holds them); what stays is the fixture two CORE rows read: avatar.responsive (tests/avatarasync) and scripting.e2e.one_asset
# (tests/scripting) spawn the default avatar from beta_mannequin.dae and load its walk from beta_walk.dae. Ships 3 and 4
# (8d2ca389b, 028cd1886) went red on both in a fresh tree because these generators went with the suites while main's
# build-linux still held the files they had made — a proof on main must delete build-linux/tests/avatar/fixtures first.
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

# beta_walk.dae — the other half of the pair (verb-coverage audit F5): the SAME Mixamo Beta export with the BODY stripped
# instead of the clip, and its animation renamed "Walking" — an animation-only file, the shape Mixamo hands you for a
# "without skin" download, and the fixture avatar.loadClip exists for. The two share their joint names by construction,
# which is the join key a skeletal clip uses.
set(JAHSHAKA_BETA_WALK_DAE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/beta_walk.dae"
    CACHE INTERNAL "generated animation-only Mixamo Beta walk clip")
add_custom_command(OUTPUT "${JAHSHAKA_BETA_WALK_DAE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_beta_walk.py"
            "${CMAKE_SOURCE_DIR}" "${JAHSHAKA_BETA_WALK_DAE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_beta_walk.py"
            "${CMAKE_SOURCE_DIR}/scenes/Skeletal Animation.zip"
    COMMENT "Deriving beta_walk.dae (animation-only Mixamo Beta walk clip) from the sample archive"
    VERBATIM)
add_custom_target(beta_walk ALL DEPENDS "${JAHSHAKA_BETA_WALK_DAE}")
