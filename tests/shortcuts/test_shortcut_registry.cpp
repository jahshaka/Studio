// shortcuts.registry — unit test of the ShortcutRegistry service
// (EDITOR_SHORTCUTS_SPEC §1/§6): defaults, persistence round-trip, conflict
// refusal, unbinding, fixed rows, reset semantics. Runs offscreen; the
// QShortcuts are real but never activated.
//
// Phase C additions: SnapSettings defaults, clamping, step lists and
// persistence — the values the gizmos' Ctrl-snap and the grid spacing read.

#include <QApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QSettings>
#include <QShortcut>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest/QTest>
#include <cstdio>

#include "document/input/inputmap.h"
#include "services/shortcutregistry.h"
#include "viewport/snapsettings.h"

// The play-mode key path (AVATAR_LOCOMOTION_SPEC §8.3). This stub is the two
// real viewports' event() bodies: both EngineSceneViewport and EnginePlayerView
// call iris::gameplayClaimsKey and then feed the key to the input state, and
// this is the SAME function, not a copy of the rule. What the stub cannot be is
// an EngineSceneViewport — that one needs an engine, a view and a document.
class PlayViewStub : public QWidget
{
public:
    bool playing = false;
    int overridesSeen = 0;
    void setFocusForTest() { setFocusPolicy(Qt::StrongFocus); setFocus(); }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ShortcutOverride) {
            ++overridesSeen;
            if (iris::gameplayClaimsKey(playing, static_cast<QKeyEvent *>(e)->key())) {
                e->accept();
                return true;
            }
        }
        return QWidget::event(e);
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (!e->isAutoRepeat()) iris::InputSystem::instance().keyPressed(e->key());
    }
    void keyReleaseEvent(QKeyEvent *e) override
    {
        if (!e->isAutoRepeat()) iris::InputSystem::instance().keyReleased(e->key());
    }
};

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    const QString iniPath = dir.filePath("jahsettings.ini");

    QWidget window;

    // ---- defaults + registration ----
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        ShortcutRegistry reg(&ini);
        int fired = 0;
        reg.add("tool.translate", "Translate Tool", "Tools", QKeySequence(Qt::Key_W),
                &window, [&fired]() { ++fired; });
        reg.add("tool.rotate", "Rotate Tool", "Tools", QKeySequence(Qt::Key_E),
                &window, nullptr);
        reg.addFixed("camera.fly", "Fly Camera", "Camera", "RMB + WASD");

        CHECK(reg.entries().size() == 3, "three entries registered");
        CHECK(reg.sequence("tool.translate") == QKeySequence(Qt::Key_W), "default binding W");
        CHECK(reg.entries()[0].shortcut != nullptr, "remappable entry owns a QShortcut");
        CHECK(reg.entries()[2].shortcut == nullptr, "fixed entry has no QShortcut");
        CHECK(reg.add("tool.translate", "dup", "Tools", QKeySequence(), &window, nullptr) == nullptr,
              "duplicate id refused");

        // ---- conflict refusal ----
        QString conflict;
        CHECK(!reg.setBinding("tool.rotate", QKeySequence(Qt::Key_W), &conflict),
              "rebind onto a taken key refused");
        CHECK(conflict == "tool.translate", "conflict names the holding entry");
        CHECK(reg.sequence("tool.rotate") == QKeySequence(Qt::Key_E), "refused rebind left binding intact");
        CHECK(!reg.setBinding("camera.fly", QKeySequence(Qt::Key_Z)), "fixed row cannot be rebound");
        CHECK(!reg.setBinding("no.such.id", QKeySequence(Qt::Key_Z)), "unknown id refused");

        // ---- rebinding + the QShortcut follows ----
        CHECK(reg.setBinding("tool.rotate", QKeySequence(Qt::Key_R)), "rebind to a free key accepted");
        CHECK(reg.entries()[1].shortcut->key() == QKeySequence(Qt::Key_R), "QShortcut follows the rebind");
        // W is free after the holder moves away
        CHECK(reg.setBinding("tool.translate", QKeySequence(Qt::Key_T)), "holder can move");
        CHECK(reg.setBinding("tool.rotate", QKeySequence(Qt::Key_W)), "freed key can be taken");

        // ---- unbind ----
        CHECK(reg.setBinding("tool.translate", QKeySequence()), "unbinding (empty) accepted");
        CHECK(reg.sequence("tool.translate").isEmpty(), "unbound entry has no sequence");
        ini.sync();
    }

    // ---- persistence round-trip: a fresh registry over the same ini ----
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        ShortcutRegistry reg(&ini);
        reg.add("tool.translate", "Translate Tool", "Tools", QKeySequence(Qt::Key_W), &window, nullptr);
        reg.add("tool.rotate", "Rotate Tool", "Tools", QKeySequence(Qt::Key_E), &window, nullptr);
        CHECK(reg.sequence("tool.translate").isEmpty(), "persisted unbind survives restart");
        CHECK(reg.sequence("tool.rotate") == QKeySequence(Qt::Key_W), "persisted rebind survives restart");

        // ---- reset ----
        CHECK(reg.resetBinding("tool.rotate"), "reset accepted");
        CHECK(reg.sequence("tool.rotate") == QKeySequence(Qt::Key_E), "reset restores the default");
        reg.resetAll();
        CHECK(reg.sequence("tool.translate") == QKeySequence(Qt::Key_W), "resetAll restores every default");
        ini.sync();
    }

    // ---- after resetAll nothing is persisted ----
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        CHECK(!ini.contains("shortcut/tool.translate") && !ini.contains("shortcut/tool.rotate"),
              "defaults leave no override keys behind");
    }

    // ================= SnapSettings (phase C) =================
    // Unbound: pure defaults — the values the gizmos' Ctrl-snap reads
    // (Gizmo::snap(value, SnapSettings::xxxSize())) and the grid's spacing.
    {
        SnapSettings::bindSettings(nullptr);
        SnapSettings::reset();
        CHECK(SnapSettings::translateSize() == 1.0f, "translate snap defaults to 1.0");
        CHECK(SnapSettings::rotateSize() == 10.0f, "rotate snap defaults to 10 degrees");
        CHECK(SnapSettings::scaleSize() == 0.25f, "scale snap defaults to 0.25");

        SnapSettings::setTranslateSize(0.0f);
        CHECK(SnapSettings::translateSize() == 0.01f, "translate snap clamps up from 0");
        SnapSettings::setTranslateSize(1000.0f);
        CHECK(SnapSettings::translateSize() == 100.0f, "translate snap clamps down from 1000");

        // [ / ] stepping through the spec's list 0.1/0.25/0.5/1/5/10
        const auto &ts = SnapSettings::translateSteps();
        CHECK(SnapSettings::stepped(ts, 1.0f, +1) == 5.0f, "step up from 1 -> 5");
        CHECK(SnapSettings::stepped(ts, 1.0f, -1) == 0.5f, "step down from 1 -> 0.5");
        CHECK(SnapSettings::stepped(ts, 10.0f, +1) == 10.0f, "step up clamps at the top");
        CHECK(SnapSettings::stepped(ts, 0.1f, -1) == 0.1f, "step down clamps at the bottom");
        CHECK(SnapSettings::stepped(ts, 0.7f, +1) == 1.0f, "off-list value steps to the next step");
        CHECK(SnapSettings::stepped(ts, 0.7f, -1) == 0.5f, "off-list value steps to the previous step");
    }

    // Persistence round-trip through a bound QSettings.
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        SnapSettings::bindSettings(&ini);
        SnapSettings::setTranslateSize(0.5f);
        SnapSettings::setRotateSize(45.0f);
        ini.sync();
    }
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        SnapSettings::bindSettings(&ini);
        CHECK(SnapSettings::translateSize() == 0.5f, "translate snap persists");
        CHECK(SnapSettings::rotateSize() == 45.0f, "rotate snap persists");
        CHECK(SnapSettings::scaleSize() == 0.25f, "unset scale snap stays default");
        SnapSettings::reset();
        CHECK(SnapSettings::translateSize() == 1.0f && !ini.contains("snap/translate"),
              "reset restores defaults and clears the store");
        SnapSettings::bindSettings(nullptr);
    }

    // ================= the play-mode key path (AVATAR_LOCOMOTION_SPEC §8.3) ====
    //
    // THE COLLISION this exists to resolve: `tool.translate` is a
    // Qt::WindowShortcut on W and `tool.cycle` is one on Space. While the scene
    // plays, both keys belong to the character — so the viewport must withhold
    // them from the shortcut system, and ONLY while playing.
    {
        iris::InputSystem::instance().resetForTest();

        QWidget top;
        top.resize(200, 200);
        auto *stub = new PlayViewStub;
        stub->setParent(&top);
        stub->resize(200, 200);
        stub->setFocusForTest();

        QSettings ini(dir.filePath("playmode.ini"), QSettings::IniFormat);
        ShortcutRegistry reg(&ini);
        int translateFired = 0, cycleFired = 0, focusFired = 0;
        reg.add("tool.translate", "Translate Tool", "Tools", QKeySequence(Qt::Key_W),
                &top, [&translateFired]() { ++translateFired; });
        reg.add("tool.cycle", "Cycle Gizmo Mode", "Tools", QKeySequence(Qt::Key_Space),
                &top, [&cycleFired]() { ++cycleFired; });
        reg.add("camera.focus", "Focus Selection", "Camera", QKeySequence(Qt::Key_F),
                &top, [&focusFired]() { ++focusFired; });
        // The four read-only Gameplay rows MainWindow::setupShortcuts registers.
        const iris::InputMap &map = iris::InputSystem::instance().map();
        reg.addFixed("gameplay.move", "Move (play mode)", "Gameplay", "W / S / A / D");
        reg.addFixed("gameplay.jump", "Jump (play mode)", "Gameplay", "Space");
        CHECK(reg.setFixedText("gameplay.move", map.displayText(iris::InputAction::Move)),
              "a fixed row can be re-labelled from the live InputMap");
        CHECK(reg.entries()[3].fixedText == "W / S / A / D",
              "the Gameplay row shows the bound keys");
        CHECK(!reg.setFixedText("tool.translate", "nope"),
              "a REMAPPABLE row's text is its QKeySequence — setFixedText refuses it");
        CHECK(!reg.setFixedText("no.such.row", "nope"), "an unknown row is refused");

        top.show();
        QApplication::setActiveWindow(&top);
        stub->setFocus();
        QApplication::processEvents();

        // ---- BASELINE: not playing, W is the editor's ----
        const bool shortcutsLive = [&]() {
            QTest::keyClick(top.windowHandle(), Qt::Key_F);
            QApplication::processEvents();
            return focusFired > 0;
        }();
        CHECK(shortcutsLive, "baseline: a window shortcut fires at all in this environment");

        if (shortcutsLive) {
            stub->playing = false;
            QTest::keyClick(top.windowHandle(), Qt::Key_W);
            QTest::keyClick(top.windowHandle(), Qt::Key_Space);
            QApplication::processEvents();
            CHECK(translateFired == 1, "NOT playing: W still switches the gizmo to translate");
            CHECK(cycleFired == 1, "NOT playing: Space still cycles the gizmo mode");
            CHECK(iris::InputSystem::instance().state().move.isZero(),
                  "NOT playing: W never reached the input state");

            // ---- PLAYING: the same two keys belong to the character ----
            stub->playing = true;
            QTest::keyPress(top.windowHandle(), Qt::Key_W);
            QApplication::processEvents();
            CHECK(translateFired == 1,
                  "PLAYING: W does NOT change the gizmo mode (the gate row)");
            CHECK(iris::InputSystem::instance().state().move.y > 0.5f,
                  "PLAYING: W reached the input state and walks forward (the gate row)");
            QTest::keyRelease(top.windowHandle(), Qt::Key_W);
            QApplication::processEvents();
            CHECK(iris::InputSystem::instance().state().move.isZero(),
                  "PLAYING: the release reached the input state too");

            QTest::keyClick(top.windowHandle(), Qt::Key_Space);
            QApplication::processEvents();
            CHECK(cycleFired == 1, "PLAYING: Space jumps instead of cycling the gizmo");

            // An UNBOUND key is still the editor's while playing — the override
            // must be surgical, not a blanket key grab.
            const int focusBefore = focusFired;
            QTest::keyClick(top.windowHandle(), Qt::Key_F);
            QApplication::processEvents();
            CHECK(focusFired == focusBefore + 1,
                  "PLAYING: an unbound key (F) still reaches its editor shortcut");

            // ---- back to stopped: the editor gets its keys back ----
            stub->playing = false;
            iris::InputSystem::instance().clearKeys();
            QTest::keyClick(top.windowHandle(), Qt::Key_W);
            QApplication::processEvents();
            CHECK(translateFired == 2, "after stop: W switches the gizmo again");
            CHECK(iris::InputSystem::instance().state().move.isZero(),
                  "after stop: W no longer reaches the input state");
        }
        CHECK(stub->overridesSeen > 0,
              "the viewport's event() really is on the ShortcutOverride path");
        iris::InputSystem::instance().resetForTest();
    }

    std::printf(failures ? "FAILED (%d)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
