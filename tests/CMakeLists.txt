# Headless test suites. No Qt, no app: each suite links exactly one library.
#
# THE ON-SCREEN SUITES' DISPLAY is a cache variable, not a literal. A handful of
# suites need a real window (the application self-tests, the WinIdChange one),
# and they used to pin ":0" — which on a developer machine means "the owner's
# live session": a parallel worktree build had no way to run its own gate
# without opening windows on somebody else's desktop. Configure with
# -DJAH_TEST_DISPLAY=:NN to point them at an Xvfb of your own. Default unchanged.
set(JAH_TEST_DISPLAY ":0" CACHE STRING
    "X display the on-screen test suites run on (Linux only)")

add_subdirectory(engine)
add_subdirectory(document)
add_subdirectory(mirror)
add_subdirectory(particles)
add_subdirectory(picking)
add_subdirectory(app)
add_subdirectory(gizmo)
add_subdirectory(thumbnails)
add_subdirectory(player)
add_subdirectory(playmode)
add_subdirectory(skeletal)
add_subdirectory(gi)
add_subdirectory(planar)
add_subdirectory(assets)
add_subdirectory(assetdelete)
add_subdirectory(assetpaths)
add_subdirectory(assetmigrate)
add_subdirectory(shadergraph)
add_subdirectory(materialpreview)
add_subdirectory(desktops)
add_subdirectory(drawers)
add_subdirectory(assetmeta)
add_subdirectory(audiopeaks)
add_subdirectory(scripting)
add_subdirectory(services)
add_subdirectory(shortcuts)
add_subdirectory(mcp)
add_subdirectory(claudechat)
add_subdirectory(theme)
add_subdirectory(export)
add_subdirectory(importer)
add_subdirectory(importasync)
add_subdirectory(openasync)
add_subdirectory(archiveasync)
add_subdirectory(shutdown)
add_subdirectory(ui)
add_subdirectory(avatar)
# The shader cache's container (SHADER_CACHE_SPEC §4.5). AFTER tests/engine:
# it reuses that directory's JAHSHAKA_ASAN option and its sanitised backend.
add_subdirectory(shadercache)
