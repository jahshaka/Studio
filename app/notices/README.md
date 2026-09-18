# app/notices — the licence texts of the components that are NOT in this tree

Every other third-party notice this application shows is read out of the
vendored source it covers: `cmake/Notices.cmake` points the generated resource
straight at `irisgl/thirdparty/assimp/LICENSE`, at `thirdparty/qlementine/LICENSE`
and so on, so the text in the binary IS the text in the tree and cannot drift
from it. That is the rule, and `source.notices_coverage` enforces it.

**This folder is the one exception, and the reason is that these components are
not vendored at all.** Jahshaka links dynamically against Qt, and the macOS
release bundle redistributes the Vulkan loader and MoltenVK from the LunarG SDK
(`scripts/make-macos-bundle.sh`). Their licences still have to be shown — an
LGPL dependency in particular requires it — and there is no file in this
repository to read them from, so the canonical texts live here with their
provenance recorded.

| File | Covers | Where the text came from |
|---|---|---|
| `qt-lgpl-3.0.txt` | Qt 6 (LGPL-3.0) | A NOTICE header written for this file (the component, the linkage, and where the corresponding source is), then `/usr/share/common-licenses/LGPL-3` (sha256 `e3a994d82e644b03a792a930f574002658412f62407f5fee083f2555c5f23118`) and `/usr/share/common-licenses/GPL-3` (sha256 `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`) verbatim, copied 2026-09-18 — the LGPL-3 grants additional permissions over the GPL-3 and incorporates its terms by reference, so both are needed. |
| `apache-2.0.txt` | The Vulkan loader and MoltenVK, both Apache-2.0, redistributed inside the macOS bundle | `/usr/share/common-licenses/Apache-2.0` verbatim (sha256 `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`), copied 2026-09-18. It is NOT byte-identical to `app/fonts/Apache License.txt`, which is the same licence as the fonts' upstream shipped it — that one stays where it is and covers the fonts. |

Nothing here is Jahshaka's own work beyond the NOTICE header at the top of
`qt-lgpl-3.0.txt`, which states what is linked and where its source is — the
part a licence text cannot say for you.

**Adding to this folder is not the default.** A component whose source is in
this tree is read from its own directory; this folder is only for one that is
not, and `app/notices.json` marks such an entry `vendored: false` so the
coverage test can tell the two apart.
