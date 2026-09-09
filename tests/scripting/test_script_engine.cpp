// Scripting engine unit test (SCRIPTING_SPEC.md phase 0).
//
// Proves the QJSEngine host end to end with a fake module, no Studio linked:
//   - expression evaluation and JSON-native returns
//   - error reporting with script line numbers and file names
//   - console.log capture through the ScriptEngine signal
//   - one-undo-step-per-script macro wrapping (and the opt-out), including the
//     LAZY half: a run that records nothing must leave the stack untouched
//   - ApiModule precondition guards throw catchable JS errors, never crash
//   - ApiRegistry::validate() rejects undocumented/misregistered verbs
//   - api.version / api.help() / api.verbs() enumeration
//
// Runs under QT_QPA_PLATFORM=offscreen. Framework-free; non-zero exit on failure.
#include <QGuiApplication>
#include <QUndoCommand>
#include <QUndoStack>
#include <cstdio>

#include "scriptengine.h"
#include "services/undoservice.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---- a minimal module exercising values, guards and undo ----------------------

class SetValueCommand : public QUndoCommand
{
public:
    SetValueCommand(int *slot, int value) : mSlot(slot), mOld(*slot), mNew(value) {}
    void undo() override { *mSlot = mOld; }
    void redo() override { *mSlot = mNew; }
private:
    int *mSlot, mOld, mNew;
};

class FakeModule : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;
    int value = 0;
    /// The app's ONE undo sink. Commands go through it here for the same
    /// reason they do in the editor: the run's undo macro is opened by the
    /// first command that reaches the sink, so a module that pushed around it
    /// would be testing a path that does not exist in the product.
    UndoService *sink = nullptr;

    QString jsName() const override { return QStringLiteral("fake"); }
    QVector<VerbInfo> verbs() const override
    {
        return {
            { "add", "fake.add(a, b) -> number", "Adds two numbers.", Needs::Document },
            { "set", "fake.set(v)", "Sets the value through an undoable command.", Needs::Document },
            { "get", "fake.get() -> number", "Reads the value back.", Needs::Document },
            { "engineOnly", "fake.engineOnly()", "Requires the engine (always fails here).", Needs::Engine },
            { "projectOnly", "fake.projectOnly()", "Requires an open project.", Needs::Document },
            { "info", "fake.info() -> {sum, list}", "Returns a JSON object.", Needs::Document },
        };
    }

    Q_INVOKABLE double add(double a, double b) { return a + b; }
    Q_INVOKABLE void set(int v)
    {
        if (sink) sink->push(new SetValueCommand(&value, v));
        else if (host.undoStack) host.undoStack->push(new SetValueCommand(&value, v));
        else value = v;
    }
    Q_INVOKABLE int get() const { return value; }
    Q_INVOKABLE void engineOnly() { requireEngine(); }
    Q_INVOKABLE bool projectOnly() { return requireProject(); }
    Q_INVOKABLE QVariantMap info()
    {
        return { { "sum", 3 }, { "list", QVariantList{ 1, 2 } } };
    }
};

// A module with broken metadata: validate() must flag it.
class BrokenModule : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;
    QString jsName() const override { return QStringLiteral("broken"); }
    QVector<VerbInfo> verbs() const override
    {
        return {
            { "undocumented", "broken.undocumented()", "", Needs::Document },   // no doc
            { "ghost", "broken.ghost()", "Registered but not implemented.", Needs::Document },
        };
    }
    Q_INVOKABLE void undocumented() {}
    // no ghost() method on purpose
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    QUndoStack undoStack;
    UndoService undoService(&undoStack);
    ScriptHost host;
    host.undoStack = &undoStack;
    // The run's undo entry: armed at the start of the run, created by the first
    // command that lands. Exactly the wiring MainWindow does.
    host.beginUndoMacro = [&undoService](const QString &text) { undoService.beginScriptMacro(text); };
    host.endUndoMacro = [&undoService]() { undoService.endScriptMacro(); };
    bool projectIsOpen = false;
    host.projectOpen = [&projectIsOpen]() { return projectIsOpen; };
    // engineReady left unset: requireEngine() must fail cleanly, not crash.

    ScriptEngine engine(host);
    auto *fake = new FakeModule(host);
    fake->sink = &undoService;
    engine.addModule(fake);

    QStringList consoleLines;
    QObject::connect(&engine, &ScriptEngine::consoleOutput,
                     [&consoleLines](const QString &t) { consoleLines << t; });

    // ---- evaluation + JSON returns ----
    auto r = engine.evaluate("1 + 1", "expr.js", false);
    CHECK(r.ok && r.value.toInt() == 2, "1+1 evaluates to 2");

    r = engine.evaluate("fake.add(2, 40)", "expr.js", false);
    CHECK(r.ok && r.value.toDouble() == 42.0, "module verb returns through the bridge");

    r = engine.evaluate("fake.info()", "expr.js", false);
    CHECK(r.ok && r.value.toMap()["sum"].toInt() == 3
               && r.value.toMap()["list"].toList().size() == 2,
          "verbs return JSON-native maps and lists");

    // ---- error line numbers ----
    r = engine.evaluate("var x = 1;\nvar y = 2;\nthrow new Error('boom');\n", "fail.js", false);
    CHECK(!r.ok, "a thrown error fails the run");
    CHECK(r.line == 3, "the error reports line 3");
    CHECK(r.error.contains("boom"), "the error carries the message");
    CHECK(r.toString().contains("fail.js:3"), "toString() is line-anchored: file.js:N");

    r = engine.evaluate("function broken( {", "syntax.js", false);
    CHECK(!r.ok && r.line >= 1, "a syntax error reports a line number");

    r = engine.evaluate("nosuch.thing()", "ref.js", false);
    CHECK(!r.ok && r.error.contains("ReferenceError"), "ReferenceError is named in the message");

    // ---- console capture ----
    r = engine.evaluate("console.log('hello', 42, {a:1}); console.warn('w')", "log.js", false);
    CHECK(r.ok, "console.log script runs");
    CHECK(consoleLines.size() == 2 && consoleLines[0] == "hello 42 {\"a\":1}",
          "console.log stringifies objects as JSON and joins arguments");

    // ---- precondition guards ----
    r = engine.evaluate("fake.engineOnly()", "guard.js", false);
    // The message names the CAPABILITY, not the platform: since the headless
    // boot (SCENEGRAPH_SPEC §3b) a --headless run HAS an engine — the NULL
    // render system — and what these verbs are missing is rendering.
    CHECK(!r.ok && r.error.contains("no rendering engine is available"),
          "requireEngine throws a JS error (no crash)");
    r = engine.evaluate("try { fake.projectOnly(); 'not reached' } catch (e) { 'caught:' + e.message }", "guard2.js", false);
    CHECK(r.ok && r.value.toString().startsWith("caught:") && r.value.toString().contains("no project"),
          "requireProject error is catchable in-script");
    projectIsOpen = true;
    r = engine.evaluate("fake.projectOnly()", "guard3.js", false);
    CHECK(r.ok && r.value.toBool(), "requireProject passes once the probe says open");

    // ---- undo macro wrapping ----
    const int before = undoStack.count();
    r = engine.evaluate("fake.set(10); fake.set(20); fake.set(30); fake.get()", "undo.js", true);
    CHECK(r.ok && r.value.toInt() == 30, "script of three commands ran");
    CHECK(undoStack.count() == before + 1, "the whole script is ONE undo entry (macro)");
    undoStack.undo();
    CHECK(fake->value == 0, "one undo reverts the entire script run");
    undoStack.redo();
    CHECK(fake->value == 30, "one redo replays it");
    undoStack.undo();

    undoStack.clear();   // drop the undone macro (a push would truncate it anyway)
    engine.evaluate("fake.set(5); fake.set(6)", "nomacro.js", false);
    CHECK(undoStack.count() == 2, "wrapUndoMacro=false pushes commands individually");

    // ---- a run that records NOTHING leaves the stack alone -------------------
    //
    // The defect this replaced: beginMacro/endMacro ran unconditionally, and
    // QUndoStack keeps an EMPTY macro as a real entry — so describing a scene
    // (or any MCP tool call, each of which is a run) pushed a do-nothing step
    // and the user's next Ctrl+Z undid THAT instead of their last edit.
    undoStack.clear();
    fake->value = 0;
    r = engine.evaluate("fake.set(7)", "edit.js", true);
    CHECK(r.ok && undoStack.count() == 1, "an editing run leaves exactly one entry");
    const QString editText = undoStack.text(0);
    r = engine.evaluate("fake.get() + fake.add(1, 2)", "query.js", true);
    CHECK(r.ok && r.value.toInt() == 10, "the query run ran");
    CHECK(undoStack.count() == 1, "a QUERY run pushes no undo entry at all");
    CHECK(undoStack.canUndo() && undoStack.text(undoStack.index() - 1) == editText,
          "the top of the stack is still the user's last real edit");
    undoStack.undo();
    CHECK(fake->value == 0, "one Ctrl+Z after a query still undoes the EDIT");

    // ...and a run that records something after several queries is still one
    // entry, i.e. the laziness did not turn into "no macro at all".
    undoStack.clear();
    fake->value = 0;
    r = engine.evaluate("fake.get(); fake.set(1); fake.set(2); fake.get()", "mixed.js", true);
    CHECK(r.ok && undoStack.count() == 1, "a mixed run is still ONE entry");
    undoStack.undo();
    CHECK(fake->value == 0, "and that one entry reverts both of its commands");

    // ---- registry metadata ----
    CHECK(engine.registry().validate().isEmpty(), "the real module set validates clean");
    {
        ApiRegistry broken;
        auto *bad = new BrokenModule(host);
        broken.add(bad);
        const auto problems = broken.validate();
        CHECK(problems.size() == 2, "validate() finds exactly the two seeded problems");
        CHECK(!problems.filter("no doc string").isEmpty(), "an undocumented verb is rejected");
        CHECK(!problems.filter("no such invokable method").isEmpty(), "a ghost verb (metadata without method) is rejected");
        delete bad;
    }

    // ---- api object ----
    r = engine.evaluate("api.version", "api.js", false);
    CHECK(r.ok && r.value.toString() == ApiRegistry::apiVersion(), "api.version matches the registry");
    r = engine.evaluate("api.help('fake.add')", "api.js", false);
    CHECK(r.ok && r.value.toString().contains("Adds two numbers"), "api.help(verb) returns its doc");
    r = engine.evaluate("api.verbs().length", "api.js", false);
    CHECK(r.ok && r.value.toInt() == 1, "api.verbs() enumerates one module");
    r = engine.evaluate("api.verbs()[0].verbs.filter(function(v){return v.needs=='engine';}).length", "api.js", false);
    CHECK(r.ok && r.value.toInt() == 1, "per-verb needs flags survive into the schema");

    // ---- registry docs generator ----
    const QString md = engine.registry().markdown();
    CHECK(md.contains("## fake") && md.contains("fake.add(a, b)"),
          "markdown reference is generated from the registry");

    printf(failures ? "\n%d FAILURES\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}

#include "test_script_engine.moc"
