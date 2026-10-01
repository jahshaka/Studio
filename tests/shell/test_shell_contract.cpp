// shell.contract — THE MODULE CONTRACT, AS THE SHELL EXECUTES IT
// (D10-SHELL-MODULES; src/modules/studiomodule.h).
//
// A fake module is driven through the shell's own parts — the ModuleHub (the
// one loop over the modules), the PageHost (pages by id) and the ActionHost
// (the ShortcutRegistry extended to actions, menus and toolbar slots) — and
// every hook it receives is logged. The suite asserts:
//
//   * the ORDER of the hooks on boot / open / create / close / a space switch /
//     an edit command / an asset request / quit, against studiomodule.h;
//   * that what the module CONTRIBUTES lands: its page under its id, its dock
//     visible only on its page, a registry row placed after its anchor, a
//     handler on a row the shell owns that runs only on the module's space, a
//     toolbar action in a named slot and a row in a named menu;
//   * that the edit chords follow the ACTIVE space — never a fallback to
//     another space's target — and Ctrl+Z moves the QUndoGroup's active stack,
//     which a page with no document does not have (audit S4a);
//   * that shutdown() runs exactly once however many exit paths ask.
//
// No window, no engine, no database: the hub, the hosts and the registry are
// the shell's real code; only the module is fake.

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoGroup>
#include <QUndoStack>
#include <cstdio>

#include "modules/studiomodule.h"
#include "services/shortcutregistry.h"
#include "shell/actionhost.h"
#include "shell/modulehub.h"
#include "shell/pagehost.h"

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

static QStringList gLog;

class FakeModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("fake"); }
    void initialize(StudioContext &ctx) override
    {
        gLog << QStringLiteral("initialize");
        initializedWith = ctx.shellWidget;
    }
    void contribute(Contributions &c) override
    {
        gLog << QStringLiteral("contribute");
        page = new QLabel(QStringLiteral("fake page"));
        c.setPage(page);
        Contributions::Dock dock;
        dock.id = QStringLiteral("fakeDock");
        dock.title = QStringLiteral("Fake");
        dock.widget = new QLabel(QStringLiteral("fake dock"));
        c.addDock(dock);
        // A row of its own, placed after the shell's tool.cycle.
        Contributions::Shortcut own;
        own.id = QStringLiteral("fake.do");
        own.label = QStringLiteral("Do The Fake Thing");
        own.category = QStringLiteral("Tools");
        own.keys = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F);
        own.after = QStringLiteral("tool.cycle");
        own.run = []() { gLog << QStringLiteral("action:fake.do"); };
        c.addShortcut(own);
        // A handler on a row the SHELL defines, for this module's space only.
        Contributions::Shortcut handler;
        handler.id = QStringLiteral("tool.cycle");
        handler.space = id();
        handler.run = []() { gLog << QStringLiteral("action:tool.cycle@fake"); };
        c.addShortcut(handler);
        toolbarAction = new QAction(QStringLiteral("Fake Tool"));
        toolbarAction->setObjectName(QStringLiteral("actionFake"));
        c.addToolbarAction(QStringLiteral("editor.end"), toolbarAction);
        menuAction = new QAction(QStringLiteral("Fake Row"));
        c.addMenuRow(QStringLiteral("view.options"), menuAction);
        c.opensAssetKind(QStringLiteral("fakeasset"));
    }
    void registerApi(ScriptEngine &) override { gLog << QStringLiteral("registerApi"); }
    void onProjectChanged(Project *p) override
    {
        gLog << (p ? QStringLiteral("project:open") : QStringLiteral("project:closed"));
    }
    void onSpaceChanged(const QString &from, const QString &to) override
    {
        gLog << QStringLiteral("space:%1>%2").arg(from, to);
    }
    EditTarget editTarget() override
    {
        gLog << QStringLiteral("editTarget");
        EditTarget t;
        t.undoStack = &stack;
        t.deleteSelection = []() { gLog << QStringLiteral("edit:delete"); };
        t.paste = []() { gLog << QStringLiteral("edit:paste"); };
        return t;
    }
    bool openAsset(const AssetRef &ref) override
    {
        gLog << QStringLiteral("asset:%1").arg(ref.guid);
        return true;
    }
    void abortBackgroundWork() override { gLog << QStringLiteral("abort"); }
    void shutdown() override { gLog << QStringLiteral("shutdown"); }

    QWidget *initializedWith = nullptr;
    QWidget *page = nullptr;
    QAction *toolbarAction = nullptr;
    QAction *menuAction = nullptr;
    QUndoStack stack;
};

class CountingCommand : public QUndoCommand
{
public:
    explicit CountingCommand(int *value) : mValue(value) {}
    void redo() override { ++*mValue; }
    void undo() override { --*mValue; }
private:
    int *mValue;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("shortcuts.ini")), QSettings::IniFormat);

    QMainWindow window;
    auto *stack = new QStackedWidget;
    window.setCentralWidget(stack);
    QString currentSpace = QStringLiteral("desktop");

    ShortcutRegistry registry(&settings);
    PageHost pages(stack, &window);
    ActionHost actions(&registry, &window, [&currentSpace]() { return currentSpace; });
    ModuleHub hub;

    // The shell's own pages, toolbar slot and menu, as the window builds them.
    pages.addPage(QStringLiteral("desktop"), new QLabel(QStringLiteral("desktop")));
    pages.addPage(QStringLiteral("editor"), new QLabel(QStringLiteral("editor")));
    QToolBar toolbar;
    toolbar.addAction(QStringLiteral("Undo"));
    QMenu viewOptions;
    viewOptions.addAction(QStringLiteral("Light Bounds"));

    // ---- boot -------------------------------------------------------------
    auto *fake = new FakeModule;
    hub.setModules({ fake });
    StudioContext ctx;
    ctx.shellWidget = &window;
    hub.initialize(ctx);
    hub.contribute(&pages, &actions);
    // The window builds its toolbar and menus AFTER the modules contributed
    // (the order it really runs in): the contributions wait for their slot.
    actions.addToolbarSlot(&toolbar, QStringLiteral("editor.end"));
    actions.registerMenu(QStringLiteral("view.options"), &viewOptions);
    // The fake never touches the engine it is handed: the reference only
    // proves the hub forwards it, in order.
    hub.registerApi(*reinterpret_cast<ScriptEngine *>(&window));

    // The shell's rows, defined AFTER the modules contributed (the order the
    // window really runs in): the anchor and the handler must still resolve.
    Contributions::Shortcut cycle;
    cycle.id = QStringLiteral("tool.cycle");
    cycle.label = QStringLiteral("Cycle Gizmo Mode / Node Search");
    cycle.category = QStringLiteral("Tools");
    cycle.keys = QKeySequence(Qt::Key_Space);
    cycle.space = QStringLiteral("editor");
    cycle.run = []() { gLog << QStringLiteral("action:tool.cycle@editor"); };
    actions.addRow(cycle);
    Contributions::Shortcut del;
    del.id = QStringLiteral("edit.delete");
    del.label = QStringLiteral("Delete Selection");
    del.category = QStringLiteral("Editing");
    del.keys = QKeySequence(Qt::Key_Delete);
    del.run = [&hub, &currentSpace]() { hub.runEdit(currentSpace, ModuleHub::Edit::Delete); };
    actions.addRow(del);
    actions.commit();

    CHECK(gLog == QStringList({ "initialize", "contribute", "registerApi" }),
          "boot: initialize -> contribute -> registerApi, once each");
    CHECK(fake->initializedWith == &window, "boot: the context reaches the module");
    CHECK(pages.page(QStringLiteral("fake")) == fake->page, "contribute: the page is keyed by the module id");
    QStringList ids;
    for (const auto &e : registry.entries()) ids << e.id;
    CHECK(ids == QStringList({ "tool.cycle", "fake.do", "edit.delete" }),
          "contribute: the module's row sits after its anchor, the shell's row is not redefined");
    CHECK(registry.entries().value(0).label == QStringLiteral("Cycle Gizmo Mode / Node Search"),
          "contribute: a handler-only contribution never defines the shell's row");
    CHECK(toolbar.actions().contains(fake->toolbarAction)
              && toolbar.actions().indexOf(fake->toolbarAction) == 1,
          "contribute: the toolbar action lands in its slot, even one built after the contribution");
    CHECK(viewOptions.actions().contains(fake->menuAction), "contribute: the menu row lands in its menu");
    auto *fakeDock = window.findChild<QDockWidget *>(QStringLiteral("fakeDock"));
    CHECK(fakeDock && fakeDock->isHidden(), "contribute: the module's dock is hidden off its page");

    // ---- open / create / close ---------------------------------------------
    gLog.clear();
    Project *someProject = reinterpret_cast<Project *>(&settings);   // never dereferenced
    hub.projectChanged(someProject);   // open
    hub.projectChanged(someProject);   // create
    hub.projectChanged(nullptr);       // close
    CHECK(gLog == QStringList({ "project:open", "project:open", "project:closed" }),
          "open / create / close: onProjectChanged(project | nullptr)");

    // ---- space switch ------------------------------------------------------
    gLog.clear();
    pages.show(QStringLiteral("fake"));
    hub.spaceChanged(currentSpace, QStringLiteral("fake"));
    currentSpace = QStringLiteral("fake");
    CHECK(gLog == QStringList({ "space:desktop>fake" }), "switch: onSpaceChanged(from, to)");
    CHECK(pages.isCurrent(QStringLiteral("fake")), "switch: the page is shown by id");
    CHECK(fakeDock && !fakeDock->isHidden(), "switch: the module's dock comes up with its page");

    // ---- an edit command and the module's chords ---------------------------
    gLog.clear();
    actions.trigger(QStringLiteral("edit.delete"));
    actions.trigger(QStringLiteral("tool.cycle"));
    actions.trigger(QStringLiteral("fake.do"));
    CHECK(gLog == QStringList({ "editTarget", "edit:delete", "action:tool.cycle@fake", "action:fake.do" }),
          "edit: the chord resolves the ACTIVE space's target; the row's handler is the space's");
    gLog.clear();
    CHECK(!hub.runEdit(currentSpace, ModuleHub::Edit::Cut), "edit: a chord the target leaves unset answers nothing");

    gLog.clear();
    pages.show(QStringLiteral("editor"));
    hub.spaceChanged(currentSpace, QStringLiteral("editor"));
    currentSpace = QStringLiteral("editor");
    actions.trigger(QStringLiteral("tool.cycle"));
    actions.trigger(QStringLiteral("edit.delete"));
    CHECK(gLog == QStringList({ "space:fake>editor", "action:tool.cycle@editor" }),
          "edit: off the module's space its handler and its target are never asked");
    CHECK(fakeDock && fakeDock->isHidden(), "switch: the dock goes with its page");

    // ---- undo follows the space (the QUndoGroup; audit S4a) ------------------
    // The owner's report: Ctrl+Z on a page that is not the editor undid the
    // scene, invisibly. The shell's own space ("editor") registers its target
    // the way the window does; the fake's stack is the module's.
    gLog.clear();
    QUndoStack editorStack;
    int sceneEdits = 0;
    int graphEdits = 0;
    editorStack.push(new CountingCommand(&sceneEdits));
    fake->stack.push(new CountingCommand(&graphEdits));
    hub.setSpaceEditTarget(QStringLiteral("editor"), [&editorStack]() {
        EditTarget t;
        t.undoStack = &editorStack;
        return t;
    });
    CHECK(!hub.undo(QStringLiteral("desktop")) && sceneEdits == 1 && graphEdits == 1,
          "undo: on a page with no document Ctrl+Z is a no-op (the scene is not touched)");
    CHECK(hub.undoGroup()->activeStack() == nullptr, "undo: a page with no document has NO active stack");
    CHECK(hub.undo(QStringLiteral("editor")) && sceneEdits == 0 && graphEdits == 1,
          "undo: on the editor the scene's stack moves");
    CHECK(hub.undoGroup()->activeStack() == &editorStack, "undo: the editor's stack is the active one there");
    CHECK(hub.redo(QStringLiteral("editor")) && sceneEdits == 1, "redo: on the editor the scene's stack moves back");
    CHECK(hub.undo(QStringLiteral("fake")) && graphEdits == 0 && sceneEdits == 1,
          "undo: on a module's space its OWN stack moves, never the scene's");
    CHECK(hub.undoGroup()->activeStack() == &fake->stack, "undo: the module's stack is active on its space");
    CHECK(!hub.redo(QStringLiteral("assets")) && graphEdits == 0 && sceneEdits == 1,
          "redo: on another page with no document nothing moves");

    // ---- an asset of the module's kind --------------------------------------
    gLog.clear();
    AssetRef ref;
    ref.guid = QStringLiteral("g-1");
    ref.kind = QStringLiteral("fakeasset");
    CHECK(hub.openAsset(ref), "asset: a contributed kind routes to its module");
    ref.kind = QStringLiteral("nobody");
    CHECK(!hub.openAsset(ref), "asset: an unowned kind routes nowhere");
    CHECK(gLog == QStringList({ "asset:g-1" }), "asset: openAsset(ref)");

    // ---- quit ----------------------------------------------------------------
    gLog.clear();
    hub.abortBackgroundWork();
    hub.shutdownModules();     // the window close (step 3)
    hub.shutdownModules();     // ...and a second exit path asking again
    CHECK(gLog == QStringList({ "abort", "shutdown" }), "quit: abort, then shutdown EXACTLY once");
    gLog.clear();
    hub.setSpaceEditTarget(QStringLiteral("editor"), nullptr);
    hub.releaseModules();      // the window body (step 5): no second shutdown
    CHECK(gLog.isEmpty() && hub.modules().isEmpty(), "quit: release deletes without a second shutdown");

    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
