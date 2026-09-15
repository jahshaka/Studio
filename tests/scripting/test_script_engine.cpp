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
//   - THE WORKER THREAD (SCRIPTING_LIVE_SPEC): every one of the cases above now
//     runs the JavaScript on ScriptWorker's thread and every verb through the
//     bridge's blocking hop, so this file is also the bridge's test — name and
//     arity dispatch, QVariant conversion, void returns, the fail() reroute.
//     Plus what only a free UI thread makes testable: Stop mid-loop, the
//     re-entrancy refusal, and the per-verb hop cost, measured.
//
// Runs under QT_QPA_PLATFORM=offscreen. Framework-free; non-zero exit on failure.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <cstdio>
#include <stdexcept>

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
            // What project.close/open/create do to the run's undo entry and to
            // the database's gesture batch, with none of their machinery
            // (CLOSE-2 round 2, H4).
            { "boundary", "fake.boundary()", "Crosses a project boundary.", Needs::Document },
            { "kinds", "fake.kinds(i, b, m) -> string", "Reports what the bridge converted.", Needs::Document },
            { "boom", "fake.boom()", "Throws a C++ exception.", Needs::Document },
            { "defaults", "fake.defaults(a, b, c) -> string", "Exercises default arguments.", Needs::Document },
            { "reenter", "fake.reenter() -> string", "Starts a second run from inside a verb.", Needs::Document },
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
    /// EXACTLY what ProjectApi::close/open/openAsync/create do around their
    /// MainWindow call, and nothing else.
    Q_INVOKABLE void boundary()
    {
        host.endRunUndoMacro();
        host.beginRunUndoMacro();
    }
    /// What the bridge made of three JavaScript values (round 2, M1).
    Q_INVOKABLE QString kinds(int i, bool b, const QVariantMap &m)
    {
        return QStringLiteral("%1/%2/%3").arg(i).arg(b ? "true" : "false").arg(m.size());
    }
    /// Throws from C++ INSIDE a verb. On the old wrapping engine this would
    /// have unwound through V4's frames; the bridge catches it and the script
    /// sees a normal JS error.
    Q_INVOKABLE bool boom() { throw std::runtime_error("verb exploded"); }
    /// Default arguments: moc emits one method per prefix, and the bridge has
    /// to pick the right one by arity.
    Q_INVOKABLE QString defaults(int a, const QString &b = QStringLiteral("b"), double c = 2.5)
    {
        return QStringLiteral("%1/%2/%3").arg(a).arg(b).arg(c);
    }
    /// A verb that RE-ENTERS the engine, which is what a verb that pumps the
    /// event loop can do for real (project.open, an import, a progress dialog).
    /// The second run must be refused, not nested.
    ScriptEngine *engine = nullptr;
    Q_INVOKABLE QString reenter()
    {
        if (!engine) return QStringLiteral("no engine");
        return engine->evaluate(QStringLiteral("fake.get()"), QStringLiteral("nested.js"), false).error;
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
    fake->engine = &engine;
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

    // ---- THE PROJECT BOUNDARY IS A NO-OP IN A RUN WITH NO UNDO ENTRY -------
    //
    // CLOSE-2 round 2, H4. The project verbs end the run's undo entry and open
    // a fresh one around a close/open (ScriptHost::endRunUndoMacro), and the
    // same bracket carries the database's gesture batch — a COUNTED scope. The
    // MCP tools evaluate small internal expressions with wrapUndoMacro FALSE
    // (scripting/mcp/mcptools.cpp), and such a run has no entry: the "end"
    // half correctly did nothing, but the "begin" half used to open a macro
    // and a batch that nothing would ever close — a transaction held for the
    // rest of the session. Both halves check the run's wrap flag now.
    {
        undoStack.clear();
        int batchDepth = 0;
        host.macroOpenChanged = [&batchDepth](bool open) { batchDepth += open ? 1 : -1; };

        // A run WITHOUT an entry: the boundary must move nothing at all.
        r = engine.evaluate("fake.boundary()", "nomacro-boundary.js", false);
        CHECK(r.ok, "a wrapUndoMacro=false run reached the project boundary");
        CHECK(batchDepth == 0,
              "...and it opened no database scope (both halves of the bracket stood down)");
        CHECK(!undoService.isScriptMacroOpen(), "...and no run macro is left open");

        // A run WITH one: the boundary ends the entry and opens a fresh one,
        // and the scope is balanced again by the end of the run.
        fake->value = 0;
        r = engine.evaluate("fake.set(1); fake.boundary(); fake.set(2)", "boundary.js", true);
        CHECK(r.ok, "a wrapped run reached the boundary");
        CHECK(batchDepth == 0, "...and its database scope is balanced at the end of the run");
        CHECK(undoStack.count() == 2,
              "...and the boundary split the run into TWO undo entries, not one");
        CHECK(!undoService.isScriptMacroOpen(), "...with nothing left armed");
        host.macroOpenChanged = nullptr;
        undoStack.clear();
    }

    // ---- the bridge: arity, conversion, C++ exceptions -----------------------
    //
    // Every verb call is now a name+arity lookup over the QMetaObject and a
    // QVariant conversion per parameter, so the shapes that used to be QJSEngine's
    // problem are ours.
    r = engine.evaluate("fake.defaults(1)", "bridge.js", false);
    CHECK(r.ok && r.value.toString() == "1/b/2.5", "default arguments: the 1-arg overload is chosen");
    r = engine.evaluate("fake.defaults(1, 'x')", "bridge.js", false);
    CHECK(r.ok && r.value.toString() == "1/x/2.5", "default arguments: the 2-arg overload is chosen");
    r = engine.evaluate("fake.defaults(1, 'x', 9)", "bridge.js", false);
    CHECK(r.ok && r.value.toString() == "1/x/9", "default arguments: all three, JS numbers converted");
    r = engine.evaluate("fake.add(2, 40, 'ignored')", "bridge.js", false);
    CHECK(r.ok && r.value.toDouble() == 42.0, "a surplus argument is dropped, as in JavaScript");
    r = engine.evaluate("typeof fake.nosuchverb", "bridge.js", false);
    CHECK(r.ok && r.value.toString() == "undefined",
          "a name the registry does not list does not exist on the module object");
    r = engine.evaluate("try { fake.boom() } catch (e) { 'caught:' + e.message }", "bridge.js", false);
    CHECK(r.ok && r.value.toString().contains("verb exploded"),
          "a C++ exception inside a verb becomes a catchable JS error");

    // ---- ARGUMENT CONVERSION SPEAKS JAVASCRIPT, not QVariant (round 2, M1) ---
    //
    // V4 used to marshal these arguments. QVariant::convert disagrees with it,
    // and two of the disagreements are wrong answers a script would never see
    // coming: convert ROUNDS a double into an int (so editor.frame(3.7) would
    // render four frames where it used to render three) and REFUSES JS null
    // outright (so an agent passing null for "no opinion" would get a thrown
    // error from twenty int-typed verbs instead of the default they document).
    fake->value = 0;
    r = engine.evaluate("fake.set(3.7); fake.get()", "convert.js", false);
    CHECK(r.ok && r.value.toInt() == 3, "a double into an int TRUNCATES toward zero, as JS does");
    r = engine.evaluate("fake.set(-3.7); fake.get()", "convert.js", false);
    CHECK(r.ok && r.value.toInt() == -3, "...toward zero on the negative side too, not down");
    r = engine.evaluate("fake.set(null); fake.get()", "convert.js", false);
    CHECK(r.ok && r.value.toInt() == 0, "JS null into an int is the parameter's default, not a throw");
    r = engine.evaluate("fake.kinds(null, null, null)", "convert.js", false);
    CHECK(r.ok && r.value.toString() == "0/false/0",
          "...and into a bool and a map as well: 0, false, empty");
    r = engine.evaluate("fake.kinds(2.9, true, {a:1})", "convert.js", false);
    CHECK(r.ok && r.value.toString() == "2/true/1", "the ordinary case is unchanged");
    // THE ONE DELIBERATE DIFFERENCE, pinned so it cannot drift silently: a
    // string into a bool follows Qt, not JavaScript's truthiness.
    r = engine.evaluate("fake.kinds(0, 'false', {})", "convert.js", false);
    CHECK(r.ok && r.value.toString() == "0/false/0",
          "a string into a bool reads the WORD (Qt), not JS truthiness — documented");

    // ---- console.log ORDER survives the thread hop ---------------------------
    //
    // The lines are emitted on the worker and re-emitted on this thread; the
    // run's completion is posted from the same thread after them, so every line
    // must have arrived, in order, by the time evaluate() returns.
    consoleLines.clear();
    r = engine.evaluate("for (var i = 0; i < 6; ++i) { console.log('n' + i); fake.get(); }",
                        "order.js", false);
    QStringList wanted;
    for (int i = 0; i < 6; ++i) wanted << QStringLiteral("n%1").arg(i);
    CHECK(r.ok && consoleLines == wanted,
          "console.log arrives in order, interleaved with verbs, before evaluate() returns");

    // ---- ONE RUN AT A TIME ---------------------------------------------------
    //
    // A verb that spins the event loop can dispatch a console click or an MCP
    // request mid-run. Nesting would evaluate into the same worker engine from a
    // second stack AND close the outer run's undo macro on its way out, so the
    // second run is refused — which is what keeps ScriptHost's macro bracket
    // (a single flag) impossible to unbalance.
    undoStack.clear();
    fake->value = 0;
    r = engine.evaluate("fake.set(3); fake.reenter()", "reentry.js", true);
    CHECK(r.ok && r.value.toString().contains("already running"),
          "a run started from inside a verb is REFUSED");
    CHECK(undoStack.count() == 1, "...and the outer run is still exactly one undo entry");
    CHECK(engine.registry().validate().isEmpty(), "the refusal left the registry alone");

    // ---- an error at verb k leaves ONE step holding the k-1 edits -------------
    undoStack.clear();
    fake->value = 0;
    r = engine.evaluate("fake.set(1); fake.set(2); throw new Error('mid'); fake.set(3);",
                        "partial.js", true);
    CHECK(!r.ok && r.error.contains("mid"), "the run failed at the throw");
    CHECK(undoStack.count() == 1, "a failed run is still ONE undo entry");
    CHECK(fake->value == 2, "...holding the edits it managed to make");
    undoStack.undo();
    CHECK(fake->value == 0, "...and one Ctrl+Z takes all of them back");

    // ---- STOP: setInterrupted from this thread, mid-loop ----------------------
    //
    // The UI thread is free while the script runs, which is what makes Stop a
    // button rather than a wish: a plain QTimer here fires INSIDE the wait.
    undoStack.clear();
    fake->value = 0;
    {
        QTimer stopper;
        stopper.setSingleShot(true);
        QObject::connect(&stopper, &QTimer::timeout, [&engine]() { engine.stop(); });
        stopper.start(150);
        QElapsedTimer spin;
        spin.start();
        r = engine.evaluate("fake.set(42); while (true) { }", "stop.js", true);
        const qint64 elapsed = spin.elapsed();
        CHECK(!r.ok && r.error.contains("stopped"), "Stop ends the run");
        printf("note: Stop landed after %lld ms of a while(true)\n", (long long)elapsed);
        CHECK(elapsed < 5000, "...within a moment, not a hang");
        CHECK(undoStack.count() == 1, "...the macro closed around the edits it had made");
        CHECK(!engine.isRunning(), "...and the engine is idle again");
    }
    r = engine.evaluate("1 + 1", "after-stop.js", false);
    CHECK(r.ok && r.value.toInt() == 2, "a stopped run does not poison the next one");

    // ---- THE HOP, MEASURED ---------------------------------------------------
    //
    // Not an assertion (a loaded box would make it one that fails): a printed
    // number, because "6 microseconds per verb" is the claim the whole design
    // rests on and it should be re-read whenever this file runs.
    {
        const int calls = 20000;
        QElapsedTimer hop;
        hop.start();
        r = engine.evaluate(QStringLiteral("for (var i = 0; i < %1; ++i) fake.get();").arg(calls),
                            "hop.js", false);
        const double us = double(hop.nsecsElapsed()) / 1000.0 / calls;
        printf("note: %d verb calls through the bridge: %.2f us each\n", calls, us);
        CHECK(r.ok, "the hop benchmark ran");
    }

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

    // ---- verb tracing (MCP session logging, ledger §361) ----
    // The bridge IS the central verb dispatch, so tracing is one recorded line
    // at the one place every call passes through (it used to swap the module
    // globals for forwarding shims). The contract is unchanged: the SAME
    // answers, the same errors, and a record of what was called.
    CHECK(!engine.verbTracing(), "trace: off by default");
    engine.setVerbTracing(true);
    CHECK(engine.verbTracing(), "trace: armed");
    r = engine.evaluate("fake.add(2, 40)", "trace.js", false);
    CHECK(r.ok && r.value.toDouble() == 42.0, "trace: a traced verb returns the same answer");
    r = engine.evaluate("for (var i = 0; i < 3; ++i) fake.set(i); fake.get()", "trace.js", true);
    CHECK(r.ok && r.value.toInt() == 2, "trace: ...and undoable verbs still record commands");
    r = engine.evaluate("fake.engineOnly()", "trace.js", false);
    CHECK(!r.ok && r.error.contains("no rendering engine is available"),
          "trace: a verb's thrown error is unchanged while the trace is armed");
    QStringList trace = engine.takeVerbTrace();
    CHECK(trace.contains("fake.add") && trace.contains("fake.set x3")
              && trace.contains("fake.get") && trace.contains("fake.engineOnly"),
          "trace: every call is recorded, repeats counted");
    CHECK(engine.takeVerbTrace().isEmpty(), "trace: taking it clears it");
    // THE CONSOLE IS NOT THE AGENT: a traced MCP run can spin the event loop,
    // and the user's own console run inside that window must not be charged to
    // it (round-2 review item 5).
    r = engine.evaluate("fake.add(1, 2)", "<console>", false);
    CHECK(r.ok && engine.takeVerbTrace().isEmpty(),
          "trace: a <console> run is NOT recorded, even while the trace is armed");
    r = engine.evaluate("fake.add(1, 2)", "mcp", false);
    CHECK(r.ok && engine.takeVerbTrace() == QStringList{ "fake.add" },
          "trace: ...and the next tool run is recorded normally");
    engine.setVerbTracing(false);
    CHECK(!engine.verbTracing(), "trace: disarmed");
    r = engine.evaluate("fake.add(1, 1)", "trace.js", false);
    CHECK(r.ok && r.value.toDouble() == 2.0, "trace: the verbs answer the same with it off");
    CHECK(engine.takeVerbTrace().isEmpty(), "trace: ...and nothing is recorded when off");

    printf(failures ? "\n%d FAILURES\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}

#include "test_script_engine.moc"
