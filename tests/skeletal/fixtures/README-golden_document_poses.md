# `golden_document_poses.txt` — the frozen oracle, and why it has no writer

`skeletal.clip_extract` compares the clip EXTRACTOR against the document's clip
EVALUATOR. That evaluator was deleted (full retirement was the point of
`ANIMATION_ENGINE_MIGRATION_SPEC`), so what the suite compares against is a
RECORDING of its answers: one line per `(fixture, clip, time, bone)` sample,
written by `test_clip_extract --write-golden` while the evaluator still existed,
and committed beside the fixtures it describes.

## Why the `--write-golden` mode is NOT coming back

The hygiene lane (2026-09-09) was asked to restore a writer that regenerates
this file "from the current evaluator path". It must not exist, and the reason
is what the file is for:

* the oracle is the **deleted** evaluator. A writer running on the tip would
  record what the EXTRACTOR says, and the suite would then compare the
  extractor against itself — green forever, and worth nothing;
* "regenerate the golden file" is the standard way a parity gate is silently
  turned off. The header of the file says so in as many words: *"REGENERATING
  THIS FILE IS NOT A WAY TO FIX A FAILING TEST."*

So the fallback the lane's brief allowed is what was done: the recipe is
archived here instead of a writer being added.

## The recipe, if the recording ever has to be redone

The samples were produced at:

| repo   | commit     |
|--------|------------|
| Studio | `dd8252eb` |
| IrisGL | `a465167`  |

— the last commits at which `SceneNode::updateAnimation` +
`SceneMirror::toBonePoses` (the document clip evaluator) existed. To redo it:

1. `git worktree add <scratch> dd8252eb` in Studio, and check IrisGL out at
   `a465167` inside it (`git -C irisgl checkout a465167`);
2. build that tree and run
   `tests/skeletal/test_clip_extract --write-golden > golden_document_poses.txt`;
3. copy the result here **only** together with an explanation of why the
   recording — not the extractor — was what changed.

Step 3 is the whole point. A new recording that is not accompanied by that
explanation is a regression being written into the fixture.

## The file's own format

    <fixture>|<clip>|<time>|<bone>  px py pz  qx qy qz qw  sx sy sz

One bone's parent-local TRS (a root bone's frame is the mesh node), exactly what
the document evaluator produced. Lines beginning `#` are comments; the reader is
`loadGolden()` in `test_clip_extract.cpp`.
