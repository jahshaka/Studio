/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// services.core — unit contracts of the Phase-1 service layer (and the folder removal's quit cap)
// (APP_ARCHITECTURE_AUDIT §3.3):
//
//  UndoService: push/undo/redo guards, the script-macro clear guard (a script
//  run must stay one undo step), and the saved-count bookkeeping including
//  the preserved bool-coercion quirk the closeEvent prompt depends on.
//
//  SelectionService: select() emits every time (re-selection re-runs the
//  panel fan-out — behaviour the panels rely on) and the re-entrancy guard
//  swallows echoes from within the fan-out.

#include <QGuiApplication>
#include <QUndoCommand>
#include <QUndoStack>

#include <cstdio>
#include <future>
#include <thread>

#include "services/apppaths.h"
#include "services/bootreport.h"
#include "services/selectionservice.h"
#include "services/uistep.h"
#include "services/undoservice.h"
#include "player/editpose.h"
#include "app/firstrun.h"
#include "services/closeunsaved.h"
#include "services/folderremoval.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"

#include <QHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QMap>
#include <memory>
#include <string>
#include <unordered_map>

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

struct CountingCommand : QUndoCommand
{
    int *redos, *undos;
    CountingCommand(int *r, int *u) : redos(r), undos(u) {}
    void redo() override { ++*redos; }
    void undo() override { ++*undos; }
};

void testUndoService()
{
    QUndoStack stack;
    UndoService undo(&stack);

    // undo/redo on an empty stack are safe no-ops (the canUndo/canRedo guards).
    undo.undo();
    undo.redo();
    CHECK(stack.count() == 0, "undo: empty-stack guards are no-ops");

    int redos = 0, undos = 0;
    undo.push(new CountingCommand(&redos, &undos));
    CHECK(stack.count() == 1 && redos == 1, "undo: push executes the command");
    CHECK(undo.isDirty(), "undo: dirty after a push");

    undo.undo();
    CHECK(undos == 1, "undo: undo runs the command's undo");
    undo.redo();
    CHECK(redos == 2, "undo: redo runs it again");

    // The script-macro guard: clear() must not clear while a run's macro is
    // OPEN. There is ONE flag for "a run is in progress" (CLOSE-2 round 2) —
    // the run's own macro, armed by beginScriptMacro — so the guard is
    // exercised through the run bracket the app really uses, not through a
    // setter that existed only to say the same thing twice.
    //
    // OPEN, NOT MERELY ARMED — this assertion MOVED (SMOKE-FIX-1's fix round,
    // 2026-09-18, with the lead's verdict). It used to read "clear() is blocked
    // while the script macro is open" and prove it with a macro that was ARMED
    // and EMPTY, which is a different state: the run's macro is armed for the
    // whole run and reaches the stack only when its first push OPENS it, and an
    // armed, empty macro holds nothing — QUndoStack will clear under one, and
    // there is no entry of the run's to lose. Blocking there made every close
    // that lands while a run has recorded nothing a NO-OP, which is exactly the
    // shape of an ASYNCHRONOUS close (the import-open path: the verb ends the
    // run's entry, re-arms, and the close happens when the archive finishes) —
    // so a departed world's commands survived the world. The contract is
    // narrower and the run is still one entry either way.
    undo.beginScriptMacro("script: guard");
    CHECK(undo.isScriptMacroOpen(), "undo: arming the run macro opens the guard");
    undo.clear();
    CHECK(stack.count() == 0,
          "undo: clear() clears under an ARMED, EMPTY run macro — nothing of the run is lost");

    // …and it is blocked once the run has actually recorded something, which is
    // when there IS an entry to corrupt (QUndoStack: "endMacro(): no matching
    // beginMacro()").
    undo.push(new CountingCommand(&redos, &undos));
    CHECK(stack.count() == 1, "undo: a push inside the run opens the run's macro");
    undo.clear();
    CHECK(stack.count() == 1, "undo: clear() is blocked while the run's macro is OPEN");
    CHECK(undo.endScriptMacro() == true,
          "undo: a run that recorded something leaves its one entry");
    CHECK(!undo.isScriptMacroOpen(), "undo: ...and the guard is closed again");
    undo.clear();
    CHECK(stack.count() == 0, "undo: clear() clears once the macro closes");

    // THE ONE DEFINITION OF "UNSAVED" (UndoService::isDirty, STUDIO-HYGIENE-1). It replaced a counter coerced
    // to 0/1, under which "2 commands still reads as saved" - this suite asserted that quirk - so every edit
    // after the first save read as saved and the close never asked about it.
    CHECK(!undo.isDirty(), "dirty: a cleared stack is a fresh document - clean");
    undo.push(new CountingCommand(&redos, &undos));
    CHECK(undo.isDirty(), "dirty: a push makes it dirty");
    undo.markSaved();
    CHECK(!undo.isDirty(), "dirty: a save makes it clean");
    undo.push(new CountingCommand(&redos, &undos));
    CHECK(undo.isDirty(), "dirty: a push AFTER a save is dirty (the coerced counter read it as saved)");
    undo.markSaved();
    undo.undo();
    CHECK(undo.isDirty(), "dirty: an undo after a save is dirty");
    undo.redo();
    CHECK(!undo.isDirty(), "dirty: the redo back to the saved state is clean");
    undo.undo();
    undo.push(new CountingCommand(&redos, &undos));
    CHECK(undo.isDirty(),
          "dirty: a push after an undo (the redo branch dropped) is dirty though the index is the saved one");
    // A FAILED save never calls markSaved (ProjectService::save): the stack stays as dirty as it was.
    CHECK(undo.isDirty(), "dirty: no markSaved, still dirty");
    // INSIDE A SCRIPT RUN'S MACRO: QUndoStack cannot set clean mid-macro, this can - and a save made inside
    // the run, with nothing pushed after it, is still the saved state once the macro closes over it.
    undo.clear();
    undo.beginScriptMacro("script: save inside");
    undo.push(new CountingCommand(&redos, &undos));
    CHECK(undo.isDirty(), "dirty: a push inside the run's macro is dirty");
    undo.markSaved();
    CHECK(!undo.isDirty(), "dirty: a save inside the run is clean");
    undo.endScriptMacro();
    CHECK(!undo.isDirty(), "dirty: ...and still clean when the macro closes over it");
    undo.beginScriptMacro("script: push after the save");
    undo.push(new CountingCommand(&redos, &undos));
    undo.markSaved();
    undo.push(new CountingCommand(&redos, &undos));
    undo.endScriptMacro();
    CHECK(undo.isDirty(), "dirty: a push after a save inside the run is dirty when the macro closes");
    undo.clear();
    CHECK(!undo.isDirty(), "dirty: clear (a close, an open) is clean");
    undo.markUnsaved();
    CHECK(undo.isDirty(), "dirty: a save that wrote no row leaves it dirty, though nothing moved");
    undo.markSaved();
    CHECK(!undo.isDirty(), "dirty: ...until a save that did");
    // NO UNDO LIMIT IS SET ANYWHERE (grep setUndoLimit: none); the push count never falls, so even a stack
    // that dropped its oldest command could not read clean after a push.
    CHECK(stack.undoLimit() == 0, "dirty: the stack has no undo limit (pushCount alone decides a push)");
}

void testSelectionService()
{
    SelectionService selection;

    int emissions = 0;
    QObject::connect(&selection, &SelectionService::selectionChanged,
                     [&emissions](iris::SceneNodePtr) { ++emissions; });

    selection.select(iris::SceneNodePtr());
    CHECK(emissions == 1, "selection: select emits");
    CHECK(!selection.selected(), "selection: null selection reads back null");

    // Re-selection re-emits: the fan-out re-runs (panels rely on the refresh).
    selection.select(iris::SceneNodePtr());
    CHECK(emissions == 2, "selection: re-selecting re-emits");

    // Re-entrancy: an echo from within the fan-out is swallowed.
    SelectionService guarded;
    int outer = 0;
    QObject::connect(&guarded, &SelectionService::selectionChanged,
                     [&guarded, &outer](iris::SceneNodePtr) {
        ++outer;
        guarded.select(iris::SceneNodePtr());   // the viewport echo
    });
    guarded.select(iris::SceneNodePtr());
    CHECK(outer == 1, "selection: re-entrant echo is swallowed by the guard");
}

/// UiStep — the label the heartbeat and the watchdog print when the UI thread
/// stops answering outside an open (FSYNC-1). Three contracts: it nests, it
/// restores, and it is READABLE FROM ANOTHER THREAD, which is the only reason
/// it is an atomic pointer to a literal instead of a QString.
void testUiStep()
{
    CHECK(UiStep::current().isEmpty(), "uistep: nothing is marked by default");
    {
        UiStep::Scope outer("archive: install import slice");
        CHECK(UiStep::current() == QStringLiteral("archive: install import slice"),
              "uistep: a scope names the step");
        {
            UiStep::Scope inner("shader cache: serializing the save");
            CHECK(UiStep::current() == QStringLiteral("shader cache: serializing the save"),
                  "uistep: a nested scope wins");

            // The watchdog reads this from its own thread while the UI thread
            // is inside the step — that is the whole use.
            QString seen;
            std::thread reader([&seen]() { seen = UiStep::current(); });
            reader.join();
            CHECK(seen == QStringLiteral("shader cache: serializing the save"),
                  "uistep: another thread reads the live step");
        }
        CHECK(UiStep::current() == QStringLiteral("archive: install import slice"),
              "uistep: leaving a nested scope restores the outer one");
    }
    CHECK(UiStep::current().isEmpty(), "uistep: leaving the last scope clears it");
}

} // namespace

// THE EDIT POSE A SAVE DURING PLAY WRITES (player/editpose.h, STUDIO-HYGIENE-1 audit): the swap over the
// play-start snapshot, on stand-in nodes — exact values in, exact values back.
struct PoseNode
{
    std::string guid;
    iris::Vec3 pos, scale{ 1, 1, 1 };
    iris::Quat rot;
    int writes = 0;
    iris::Vec3 getLocalPos() const { return pos; }
    iris::Quat getLocalRot() const { return rot; }
    iris::Vec3 getLocalScale() const { return scale; }
    void setLocalPos(const iris::Vec3 &v) { pos = v; ++writes; }
    void setLocalRot(const iris::Quat &q) { rot = q; ++writes; }
    void setLocalScale(const iris::Vec3 &v) { scale = v; ++writes; }
};
struct PoseEntry { iris::Vec3 pos, scale; iris::Quat rot; };
using PoseNodes = std::unordered_map<std::string, std::shared_ptr<PoseNode>>;   // the document's guid -> node map

void testEditPose()
{
    auto moved = std::make_shared<PoseNode>(), still = std::make_shared<PoseNode>(),
         spawned = std::make_shared<PoseNode>();
    moved->guid = "moved"; still->guid = "still"; spawned->guid = "spawned";
    moved->pos = iris::Vec3(1, 2, 3);
    still->pos = iris::Vec3(7, 0, 0);
    QMap<QString, PoseEntry> snapshot;   // what Play found
    snapshot.insert("moved", { iris::Vec3(1, 2, 3), iris::Vec3(1, 1, 1), iris::Quat() });
    snapshot.insert("still", { iris::Vec3(7, 0, 0), iris::Vec3(1, 1, 1), iris::Quat() });
    // ...and what the simulation did: the body fell and turned.
    moved->pos = iris::Vec3(1, -40.5f, 3);
    moved->rot = iris::Quat(0.7071068f, 0.7071068f, 0, 0);
    const iris::Vec3 playPos = moved->pos;
    const iris::Quat playRot = moved->rot;

    QSet<QString> spawnedGuids;
    const auto played = editpose::swapIn(PoseNodes{ { "moved", moved }, { "still", still }, { "spawned", spawned } }, snapshot,
                                         &spawnedGuids);
    CHECK(moved->pos == iris::Vec3(1, 2, 3) && moved->rot == iris::Quat(),
          "editpose: a node Play moved carries its play-start pose for the walk");
    CHECK(still->writes == 0, "editpose: a node Play never moved is not written (a static node stays static)");
    CHECK(played.size() == 1, "editpose: only the moved node is recorded to put back");
    CHECK(spawnedGuids == QSet<QString>{ "spawned" } && spawned->writes == 0,
          "editpose: a node with no snapshot entry was spawned in Play - named, left untouched");
    editpose::putBack(played);
    CHECK(moved->pos == playPos && moved->rot == playRot && moved->scale == iris::Vec3(1, 1, 1),
          "editpose: the play pose comes back exactly");
    CHECK(editpose::swapIn(PoseNodes{}, snapshot, nullptr).isEmpty(),
          "editpose: no nodes, nothing swapped");
}

// THE DOCUMENT'S EDIT POSE (player/editpose.h documentEditPose, the final read's B1): three things move one document
// — the editor's run, the Player's run, Simulate Physics. Stand-in runs: the answer is the EARLIEST-started run
// playing on the document, else the simulation's held bodies; and a run that starts over a running simulation takes
// the bodies' pre-simulate pose.
struct RunStandIn
{
    bool playingNow = false;
    quint64 serial = 0;
    int doc = 0;
    QMap<QString, PoseEntry> pose;
    bool isScenePlaying() const { return playingNow; }
    quint64 playSerial() const { return serial; }
    int document() const { return doc; }
    const QMap<QString, PoseEntry> &playStartPose() const { return pose; }
};

void testDocumentEditPose()
{
    const auto entry = [](float y) { return PoseEntry{ iris::Vec3(0, y, 0), iris::Vec3(1, 1, 1), iris::Quat() }; };
    RunStandIn editor, player;
    editor.playingNow = player.playingNow = true;
    editor.doc = player.doc = 1;
    editor.serial = 1;                      // the editor's run started first (editor.play, then the Player page)...
    player.serial = 2;
    editor.pose.insert("body", entry(5));   // ...so it holds the pose before any Play
    player.pose.insert("body", entry(-3));  // ...and the Player's snapshot is the editor's PLAY pose
    const std::unordered_map<std::string, PoseEntry> none;   // the environment's shape

    auto edit = editpose::documentEditPose<PoseEntry, RunStandIn>({ &player, &editor }, 1, &none);
    CHECK(edit.wholeDocument && edit.pose.value("body").pos == iris::Vec3(0, 5, 0),
          "editpose: two runs playing on one document - the EARLIEST-started one's snapshot is the edit pose");
    editor.playingNow = false;
    edit = editpose::documentEditPose<PoseEntry, RunStandIn>({ &editor, &player }, 1, &none);
    CHECK(edit.wholeDocument && edit.pose.value("body").pos == iris::Vec3(0, -3, 0),
          "editpose: the Player's run alone - its snapshot (the editor's run is stopped)");
    player.doc = 2;
    std::unordered_map<std::string, PoseEntry> simulated;
    simulated.insert_or_assign("body", entry(9));
    edit = editpose::documentEditPose<PoseEntry, RunStandIn>({ &editor, &player }, 1, &simulated);
    CHECK(!edit.wholeDocument && edit.pose.size() == 1 && edit.pose.value("body").pos == iris::Vec3(0, 9, 0),
          "editpose: no run on this document - Simulate's held bodies, and not a whole document (nothing is spawned)");
    edit = editpose::documentEditPose<PoseEntry, RunStandIn>({ &editor, nullptr }, 1, &none);
    CHECK(!edit.wholeDocument && edit.pose.isEmpty(), "editpose: nothing moves the document - no pose to swap");

    auto body = std::make_shared<PoseNode>(), lamp = std::make_shared<PoseNode>();
    body->guid = "body"; lamp->guid = "lamp";
    body->pos = iris::Vec3(0, -20, 0);      // Simulate Physics dropped it before Play was pressed
    lamp->pos = iris::Vec3(4, 4, 4);
    const auto snap = editpose::playStartSnapshot<PoseEntry>(PoseNodes{ { "body", body }, { "lamp", lamp } }, simulated);
    CHECK(snap.value("body").pos == iris::Vec3(0, 9, 0) && snap.value("lamp").pos == iris::Vec3(4, 4, 4) &&
              snap.size() == 2,
          "editpose: a run started over a running simulation takes the body's PRE-SIMULATE pose, every other node's own");
}

// A QUIT NEVER WAITS FOR A PROJECT FOLDER'S BYTES WITHOUT A LIMIT (the final read 2's M1; services/folderremoval.h).
// The shutdown waited for every removal in flight with no cap while the window was still on screen. Now a drain
// waits at most its cap, STOPS the worker between two entries and joins it; the folder stays aside, and the next
// start (a new owner: ProjectService's sweep) removes it. Red on the base: there the drain had no cap at all.
void testFolderRemovalCap()
{
    QTemporaryDir root;
    const auto makeTree = [&](const QString &name, int dirs, int filesPerDir) {
        const QString top = root.filePath(name);
        for (int d = 0; d < dirs; ++d) {
            const QString dir = top + QStringLiteral("/exports/d%1").arg(d);
            QDir().mkpath(dir);
            for (int f = 0; f < filesPerDir; ++f) {
                QFile file(dir + QStringLiteral("/f%1").arg(f));
                if (file.open(QIODevice::WriteOnly)) file.write("x");
            }
        }
        return top;
    };
    // A small folder inside the cap: removed whole, nothing stopped.
    const QString small = makeTree(QStringLiteral(".removing-small-0"), 2, 3);
    {
        FolderRemoval removal;
        CHECK(removal.start({ small }) == 1, "folders: a start takes the folder");
        CHECK(removal.start({ small }) == 0, "folders: ...and a second start never hands a second worker the same one");
        const FolderRemoval::Drained d = removal.drain(FolderRemoval::kQuitCapMs);
        CHECK(d.waited == 1 && d.stopped == 0 && !QFileInfo::exists(small),
              "folders: a removal that ends inside the cap is waited for and the folder is gone");
    }
    // A large folder drained at a cap of 0 (the quit's cap, reached): the drain STOPS the worker and returns; the
    // folder stays aside. 20,000 files: a worker cannot unlink them between its start and the drain's next line.
    const QString large = makeTree(QStringLiteral(".removing-large-0"), 40, 500);
    {
        FolderRemoval removal;
        removal.start({ large });
        const FolderRemoval::Drained d = removal.drain(0);
        CHECK(d.waited == 1 && d.stopped == 1, "folders: a drain past its cap stops the removal (not waited out)");
        CHECK(QFileInfo::exists(large) && !removal.owns(large),
              "folders: ...the folder stays aside, and no worker holds it any more");
    }
    // The next start (a new owner) finishes it.
    {
        FolderRemoval removal;
        removal.start({ large });
        const FolderRemoval::Drained d = removal.drain(60000);
        CHECK(d.stopped == 0 && !QFileInfo::exists(large), "folders: the next start removes the folder left aside");
    }
}

// THE TWO PATH DOORS (NOQT-CORE-1 stage 2, in place of IrisUtils): AppPaths::shipped(rel) is the old
// getAbsoluteAssetPath's text exactly (applicationDirPath + separator + rel, cleaned: a leading slash on `rel` is
// cleaned away, never "//"), and AppPaths::inDataRoot(name) is the old join(dataRoot(), name)'s text exactly
// ("<root>/<name>", cleaned, no trailing slash). Each is checked against the deleted helper's expression AND against
// the literal path it must give. Forces this process's data root (the unsaved case after it forces its own).
void testPathDoors()
{
    const QString app = QDir(QCoreApplication::applicationDirPath()).absolutePath();
    const auto oldShipped = [](const QString &rel) {    // irisutils.h's getAbsoluteAssetPath, verbatim
        return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absolutePath() + QDir::separator() + rel);
    };
    for (const QString &rel : { QStringLiteral("app/icons/icons8-file-72.png"), QStringLiteral("/app/icons/x.png"),
                                QStringLiteral("app/content/../content/materials") })
        CHECK(AppPaths::shipped(rel) == oldShipped(rel),
              qPrintable(QStringLiteral("paths: shipped(\"%1\") is the old helper's text (%2)").arg(rel, AppPaths::shipped(rel))));
    CHECK(AppPaths::shipped(QStringLiteral("/app/icons/x.png")) == app + QStringLiteral("/app/icons/x.png"),
          "paths: shipped() cleans a leading slash's doubled separator away");
    CHECK(AppPaths::shipped(QStringLiteral("app/content/../content/materials")) == app + QStringLiteral("/app/content/materials"),
          "paths: shipped() cleans a '..'");
    QTemporaryDir root;
    AppPaths::initialize(root.path());
    const QString base = QDir::cleanPath(root.path());
    const auto oldJoin = [](const QString &a, const QString &b) {   // irisutils.h's join(a, b), verbatim
        return QDir::cleanPath(a + QStringLiteral("/") + b + QStringLiteral("/"));
    };
    for (const QString &name : { QStringLiteral("JahLibrary.db"), QStringLiteral("assets/store/") })
        CHECK(AppPaths::inDataRoot(name) == oldJoin(AppPaths::dataRoot(), name),
              qPrintable(QStringLiteral("paths: inDataRoot(\"%1\") is the old join's text (%2)").arg(name, AppPaths::inDataRoot(name))));
    CHECK(AppPaths::inDataRoot(QStringLiteral("JahLibrary.db")) == base + QStringLiteral("/JahLibrary.db"),
          "paths: inDataRoot() is <the forced root>/<name>");
    CHECK(AppPaths::inDataRoot(QStringLiteral("assets/store/")) == base + QStringLiteral("/assets/store"),
          "paths: inDataRoot() keeps no trailing slash");
}

// WHO ANSWERS AN UNSAVED WORLD (services/closeunsaved.h decideUnsaved, the lead's ruling 2026-11-21): the box is a
// person's unless nobody is at the screen. A forced data root alone (an app launched with --data-root, no script,
// no MCP client) is a person's session: it is driven, but not scripted, and its close over unsaved changes ASKS.
// Run last: it forces this process's data root.
void testUnsavedDecision()
{
    QTemporaryDir root;
    AppPaths::initialize(root.path());   // what --data-root does; main()'s latch never ran: no script, no MCP
    CHECK(FirstRun::isDrivenSession() && !FirstRun::isScriptedSession(),
          "unsaved: a forced data root alone is driven, not scripted");
    CHECK(decideUnsaved(CloseUnsaved::Ask, FirstRun::isScriptedSession(), false, true, false) == UnsavedVerdict::Ask,
          "unsaved: a person's close in a --data-root session over unsaved changes, auto-save off - asked");
    CHECK(decideUnsaved(CloseUnsaved::Ask, FirstRun::isScriptedSession(), true, true, false) == UnsavedVerdict::Ask,
          "unsaved: a person's close in a --data-root session whose auto-save failed - asked");
    FirstRun::rememberScripted(true);   // a --script / --headless run or an --mcp-port client
    CHECK(decideUnsaved(CloseUnsaved::Ask, FirstRun::isScriptedSession(), false, true, false) == UnsavedVerdict::Refuse,
          "unsaved: a scripted session's close over unsaved changes - refused, never a box nobody answers");
    FirstRun::rememberScripted(false);
    CHECK(decideUnsaved(CloseUnsaved::Refuse, false, false, true, false) == UnsavedVerdict::Refuse,
          "unsaved: a script's route without {discard} - refused, even with a person at the screen");
    CHECK(decideUnsaved(CloseUnsaved::Discard, true, false, true, false) == UnsavedVerdict::Close,
          "unsaved: {discard: true} - closes");
    CHECK(decideUnsaved(CloseUnsaved::Ask, false, true, true, true) == UnsavedVerdict::Close,
          "unsaved: auto-save on and the save held the whole document - closes");
    CHECK(decideUnsaved(CloseUnsaved::Ask, true, false, false, false) == UnsavedVerdict::Close,
          "unsaved: auto-save off and nothing changed - closes");
}

// THE SWEEP's PLAN IS NEVER WAITED FOR ON A LIVE WINDOW (NOQT-CORE-1 E2's audit F1'): app.bootReport({joinSweep})
// reads SweepPlan::read(join, FirstRun::isScriptRun()) - a join only in a command-line script run, the state as it
// stands anywhere else. This process is no script run (main() never set the latch), like the editor's console.
void testSweepRead()
{
    SweepPlan plan;
    plan.start();   // the "worker" is this test: the plan stays Planning until the test publishes (an event it controls)
    CHECK(!FirstRun::isScriptRun(), "sweep: this process is not a command-line script run (the latch is unset)");
    CHECK(plan.read(true, FirstRun::isScriptRun()) == SweepPlan::State::Planning,
          "sweep: OUTSIDE a script run a joinSweep read answers AT ONCE - 'planning' - while the worker is held");
    // THE CONTROL: in a script run the read JOINS, and answers the worker's publish (sent from another thread).
    std::promise<void> go;
    std::thread worker([&plan, ready = go.get_future()]() mutable {
        ready.wait();
        plan.finish(SweepPlan::State::Planned, 3);
    });
    go.set_value();
    const SweepPlan::State joined = plan.read(true, true);
    worker.join();
    CHECK(joined == SweepPlan::State::Planned && plan.rebuilds() == 3,
          "sweep: IN a script run the read joins and answers the worker's publish (planned, 3)");
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    testSweepRead();
    testUndoService();
    testSelectionService();
    testUiStep();
    testEditPose();
    testDocumentEditPose();
    testFolderRemovalCap();
    testPathDoors();
    testUnsavedDecision();
    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
