// document.write_journal — THE WRITE JOURNAL'S DEDUPE, AT ITS THREE EDGES (SCENEGRAPH-BASELINE-2).
//
// The journal (irisgl/document/scenegraph/nodegraph.cpp) logs a node's write only when some reader would not otherwise
// see that node again: its newest entry of that kind is behind the furthest reader's cursor. The newest entry is the
// NODE's own stamp (DocNode::journalNewest), so the three places the rule meets something else are held here:
//
//   A. AN ID REPLAY (the undo of a delete: SceneEditService::rebuildFragment -> readSceneNode writes and attaches a
//      detached subtree under the ids its constructors minted, setNodeId gives each its session id, the redo attaches
//      it): the minted-id entries resolve to nothing, so the attach under the session id MUST log — a reader that
//      takes in the same frame names the node.
//   B. SEVERAL READERS: one takes between two writes of a node; the second write still reaches it (its cursor passed
//      the first entry), and the reader that did not take sees the node too.
//   C. THE CAP: more distinct writes than the log holds, no reader taking; the oldest entries go, and a node written
//      again whose entry went is deduped against its stamp — which is exact only because every reader's next take is
//      INCOMPLETE (the readers' full pass). The take says so.
//
// Counts, not time. Document only: NULL render system, no display.
#include <cstdio>
#include <algorithm>
#include <vector>

#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {
bool names(const std::vector<iris::SceneNode *> &v, const iris::SceneNode *n)
{
    return std::find(v.begin(), v.end(), n) != v.end();
}
}   // namespace

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    enginetest::DocumentGraph graph;
    if (!graph.ok()) { std::printf("FAIL: headless engine: %s\n", graph.error().c_str()); return 1; }

    // A. THE ID REPLAY.
    {
        auto scene = iris::Scene::create();
        iris::graph::WriteJournalReader reader;
        // The session id the undo restores: a node that held it and was deleted.
        qint64 sessionId = 0;
        {
            auto deleted = iris::SceneNode::create();
            sessionId = deleted->getNodeId();
        }
        // The replay: a detached fragment written under the id its constructor minted...
        auto root = iris::SceneNode::create();
        auto child = iris::SceneNode::create();
        root->addChild(child, false);
        root->setLocalPos(iris::Vec3(1.0f, 2.0f, 3.0f));
        child->setLocalPos(iris::Vec3(0.5f, 0.0f, 0.0f));
        const qint64 minted = root->getNodeId();
        // ...given its session id...
        root->setNodeId(sessionId);
        CHECK(root->getNodeId() == sessionId && minted != sessionId, "(A) the fragment root takes its session id");
        CHECK(iris::graph::nodeById(sessionId) == root.data() && iris::graph::nodeById(minted) == nullptr,
              "(A) the owner table follows the id (the minted id resolves to nothing)");
        // ...and attached in the same frame (the redo's insertChild), then a reader takes.
        scene->getRootNode()->addChild(root, false);
        std::vector<iris::SceneNode *> moved;
        reader.take(moved);
        CHECK(names(moved, root.data()), "(A) the take names the re-id'd root: its attach logged under the session id");
        // ...and a write in the same frame after it (an undo, then a script's move) is named on the next take.
        root->setLocalPos(iris::Vec3(4.0f, 5.0f, 6.0f));
        reader.take(moved);
        CHECK(names(moved, root.data()), "(A) a later write to the re-id'd root reaches the reader");
        scene->getRootNode()->removeChild(root);
    }

    // B. SEVERAL READERS.
    {
        iris::graph::WriteJournalReader first, second;
        auto n = iris::SceneNode::create();
        n->setLocalPos(iris::Vec3(1.0f, 0.0f, 0.0f));
        std::vector<iris::SceneNode *> a, b;
        first.take(a);
        CHECK(a.size() == 1 && names(a, n.data()), "(B) the first reader's take names the node, once");
        n->setLocalPos(iris::Vec3(2.0f, 0.0f, 0.0f));   // behind `first`'s new cursor: must log again
        first.take(a);
        CHECK(a.size() == 1 && names(a, n.data()), "(B) the second write reaches the reader that took between them");
        second.take(b);
        CHECK(b.size() == 1 && names(b, n.data()), "(B) the reader that never took names the node, once");
        n->setLocalPos(iris::Vec3(3.0f, 0.0f, 0.0f));
        n->setLocalPos(iris::Vec3(4.0f, 0.0f, 0.0f));   // twice in a frame: one entry
        const std::size_t before = iris::graph::writeJournalSize();
        n->setLocalPos(iris::Vec3(5.0f, 0.0f, 0.0f));
        CHECK(iris::graph::writeJournalSize() == before, "(B) a node written again before any take logs nothing more");
        first.take(a);
        second.take(b);
        CHECK(names(a, n.data()) && names(b, n.data()), "(B) both readers name the twice-written node");
    }

    // C. THE CAP (65,536 entries): more distinct writes than it holds, nobody taking.
    {
        iris::graph::WriteJournalReader lagging;
        lagging.skip();   // PRIMED: a reader's first take is incomplete by construction — this one's must be the cap's
        std::vector<iris::SceneNodePtr> nodes;
        const int count = 65536 + 64;
        nodes.reserve(std::size_t(count));
        for (int i = 0; i < count; ++i) {
            auto n = iris::SceneNode::create();
            n->setLocalPos(iris::Vec3(float(i), 0.0f, 0.0f));
            nodes.push_back(n);
        }
        CHECK(iris::graph::writeJournalSize() <= 65536u, "(C) the log stays at its cap");
        // The first node's entry went with the cap; written again it is deduped against its stamp...
        nodes.front()->setLocalPos(iris::Vec3(-1.0f, 0.0f, 0.0f));
        std::vector<iris::SceneNode *> moved;
        const bool complete = lagging.take(moved);
        // ...so the take must not claim to be complete: the reader's full pass is what sees it.
        CHECK(!complete, "(C) a take behind the cap's drop is INCOMPLETE (the reader's full pass covers the deduped write)");
        CHECK(!moved.empty() && !names(moved, nodes.front().data()),
              "(C) ...and the cap-dropped node is not in the list (the full pass, not the list, answers it)");
        nodes.front()->setLocalPos(iris::Vec3(-2.0f, 0.0f, 0.0f));
        const bool complete2 = lagging.take(moved);
        CHECK(complete2 && moved.size() == 1 && names(moved, nodes.front().data()),
              "(C) once the reader has caught up, the next write is logged and named, the take complete");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
