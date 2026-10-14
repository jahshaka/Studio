#!/usr/bin/env bash
#
# source.scene_no_friends — OgreScene HAS NO FRIEND (ENGINE-UPGRADE-2 stage 2).
#
# The ray tier read eight of OgreScene's private members (35 reads, 2 writes) through
# `friend class RayQueryTier`. A reader outside the scene now takes the GPU scene through
# its read-only view (OgreScene::substrate / gpuSlotView / gpuNodeView / markGpuNodeDirty /
# rayStructureEpoch) and Photon's products through PhotonComponent (OgreScene::photon), and
# the compiler holds the boundary — as long as no friend comes back. This counts the
# `friend` declarations inside OgreScene's class body: the bar is 0.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: scene_no_friends.sh <source-root>}"
FILE="$ROOT/irisgl/engine/src/EnginePrivate.h"
[ -f "$FILE" ] || { echo "source.scene_no_friends: FAIL — no $FILE"; exit 1; }
# The class body: from its head to the first line that is exactly `};` after it.
friends=$(awk '/^class OgreScene final : public Scene \{/ {in_=1; next}
              in_ && /^\};/ {exit}
              in_ && /^[[:space:]]*friend[[:space:]]/ {n++; print FILENAME ":" NR ": " $0 > "/dev/stderr"}
              END {print n+0}' "$FILE")
if [ "$friends" = 0 ]; then
    echo "source.scene_no_friends: ok — OgreScene declares no friend"
    exit 0
fi
echo "source.scene_no_friends: FAIL — OgreScene declares $friends friend(s) (above): a reader goes"
echo "    through the scene's views (the GPU-scene view, PhotonComponent), never its members"
exit 1
