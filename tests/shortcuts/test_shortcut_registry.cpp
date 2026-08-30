// shortcuts.registry — unit test of the ShortcutRegistry service
// (EDITOR_SHORTCUTS_SPEC §1/§6): defaults, persistence round-trip, conflict
// refusal, unbinding, fixed rows, reset semantics. Runs offscreen; the
// QShortcuts are real but never activated.
//
// Phase C additions: SnapSettings defaults, clamping, step lists and
// persistence — the values the gizmos' Ctrl-snap and the grid spacing read.

#include <QApplication>
#include <QKeySequence>
#include <QSettings>
#include <QShortcut>
#include <QTemporaryDir>
#include <QWidget>
#include <cstdio>

#include "services/shortcutregistry.h"
#include "viewport/snapsettings.h"

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

    std::printf(failures ? "FAILED (%d)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
