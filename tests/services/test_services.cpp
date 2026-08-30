/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// services.core — unit contracts of the Phase-1 service layer
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

#include "services/selectionservice.h"
#include "services/undoservice.h"

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

    // The script-macro guard: clear() must not clear while a macro is open.
    undo.setScriptMacroOpen(true);
    undo.clear();
    CHECK(stack.count() == 1, "undo: clear() is blocked while the script macro is open");
    undo.setScriptMacroOpen(false);
    undo.clear();
    CHECK(stack.count() == 0, "undo: clear() clears once the macro closes");

    // Saved-count bookkeeping, preserving the pre-extraction bool coercion:
    // the recorded value is only ever 0 or 1.
    CHECK(undo.savedCountMatchesCurrent(), "undo: fresh service matches empty stack");
    undo.push(new CountingCommand(&redos, &undos));
    CHECK(!undo.savedCountMatchesCurrent(), "undo: unsaved push is detected");
    undo.markSaved();
    CHECK(undo.savedCountMatchesCurrent(), "undo: markSaved matches");
    undo.push(new CountingCommand(&redos, &undos));
    // The quirk: 2 commands vs saved-at-1 both coerce to 1 — "matches".
    CHECK(undo.savedCountMatchesCurrent(),
          "undo: bool-coercion quirk preserved (2 commands still reads as saved)");
    undo.resetSavedCount();
    CHECK(!undo.savedCountMatchesCurrent(), "undo: resetSavedCount marks unsaved again");
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

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    testUndoService();
    testSelectionService();
    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
