#!/usr/bin/env python3
"""source.graphics_queue_seams — THE ENGINE NAMES THE GRAPHICS QUEUE ONLY WHERE IT MUST (ENGINE-UPGRADE-ASYNC-3).

Inside an async compute bracket Ogre records its compute work into the async compute queue
(VulkanDevice::_jahRecordQueue). A raw command an engine TU records into `mGraphicsQueue` directly
would land on the wrong queue there, silently — so every take of the frame's command buffer goes
through the recording queue (AtomPassContext::frameCmd, RayQueryTier::frameCmd, the monitor's
submission tag), and `mGraphicsQueue.` is named only by the lines that MUST be graphics, listed in
graphics_queue_allow.txt (the file, the line's code, how many times):
  * the OpenXR binding (the queue family and index the session hands the runtime) and the eye
    hand-over into the swapchain (refused inside a bracket by _jahRequireGraphics);
  * the ray tier's three blocking one-shot submits (readTlasBlocking, traceBlocking,
    cardPickBlocking): their own command pools on the graphics family and their vkQueueSubmit.
A code line (comments excluded) under irisgl/engine/src matching `mGraphicsQueue\\.` that the list
does not hold — or more of one than it allows — is RED. RED ON BASE 836d4270a: 15 matches, 5 not
allowed (OgreAtomPass.cpp frameCmd, OgreFrameMonitor.cpp's tag, OgreRayQuery.cpp frameCmd x2 and
traceCardMovers' endAllEncoders).

Run: graphics_queue_seams.py <source-root>
"""
import collections
import os
import re
import sys


def main():
    root = sys.argv[1]
    src = os.path.join(root, 'irisgl', 'engine', 'src')
    here = os.path.dirname(os.path.abspath(__file__))
    allow = collections.Counter()
    with open(os.path.join(here, 'graphics_queue_allow.txt')) as f:
        for line in f:
            line = line.rstrip('\n')
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            count, fname, code = line.split('\t', 2)
            allow[(fname, code.strip())] += int(count)
    found = collections.Counter()
    where = collections.defaultdict(list)
    for dirpath, _, files in os.walk(src):
        for fn in files:
            if not fn.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, errors='replace') as f:
                for no, line in enumerate(f, 1):
                    code = line.strip()
                    if code.startswith('//') or not re.search(r'mGraphicsQueue\.', code):
                        continue
                    key = (os.path.relpath(path, src), code)
                    found[key] += 1
                    where[key].append(no)
    bad = []
    for key, n in sorted(found.items()):
        if n > allow.get(key, 0):
            bad.append((key, n, allow.get(key, 0)))
    total = sum(found.values())
    if bad:
        for (fname, code), n, ok in bad:
            print('  %s:%s  %s  (%d found, %d allowed)' % (fname, ','.join(map(str, where[(fname, code)])), code, n, ok))
        print('source.graphics_queue_seams: FAIL — %d line(s) name mGraphicsQueue outside the allow-list: take the '
              'recording queue (VulkanDevice::_jahRecordQueue) or, where the work MUST be graphics, assert it '
              '(_jahRequireGraphics) and list the line in graphics_queue_allow.txt' % sum(n - ok for _, n, ok in bad))
        return 1
    print('source.graphics_queue_seams: ok — %d line(s) name mGraphicsQueue, every one allow-listed' % total)
    return 0


if __name__ == '__main__':
    sys.exit(main())
