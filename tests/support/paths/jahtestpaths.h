// jahtestpaths.h - THE TREE's PATHS, READ AT RUN TIME (the build-speed audit D1, 2026-11-23): a test's compile line carries no
// absolute path. A -D whose value names this tree (JAHSHAKA_TEST_PLUGIN_DIR, _MEDIA_DIR, _SOURCE_DIR, JAHSHAKA_BINARY, ...)
// made every TU that carried it miss ccache in every other tree (base_dir rewrites path ARGUMENTS, never -D values: 55 % of
// the box's misses by count). tests/CMakeLists.txt's jah_scope_path_defines - the one change point - keeps each such macro
// defined on the sources that read it, but as `jahtest::path("NAME")`, the same text in every tree; the VALUES live in ONE
// generated TU per tree (<build>/generated/jah_test_paths.cpp, the library jah_test_paths). The compiler force-includes this
// header on those sources (-include jahtestpaths.h through their include path), so a macro reads as before wherever a
// `const char *` serves; a string-literal concatenation (`NAME "/x"`) or QStringLiteral(NAME) becomes a runtime string.
#pragma once

namespace jahtest {
// The tree's value of the path macro `name` (its configure's value, a generator expression evaluated); an unknown name aborts
// with its name printed - a test that reads a path the build never gave it is red at once, never handed an empty string.
const char *path(const char *name);
}
