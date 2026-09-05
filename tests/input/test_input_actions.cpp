// input.actions — the gameplay action layer (AVATAR_LOCOMOTION_SPEC §8.2,
// Stage 1 gate): binding round-trip, conflict report, latch semantics.
//
// Pure iris::InputMap / InputSystem: no scene, no engine, no display. That is
// the point of putting the input layer document-side — every locomotion test
// this program will grow drives it with no synthetic key events at all.
//
// The three gate rows, and where they are:
//   binding round-trip   §"bindings persist"      (QVariantMap and QSettings)
//   conflict report      §"conflicts"             (named, atomic refusal)
//   latch semantics      §"latch vs held"         (held = continuous state,
//                                                  press = latches exactly once)

#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "document/input/inputmap.h"

using namespace iris;

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // ---- the action set is CLOSED and its metadata is fixed ----------------
    {
        CHECK(InputMap::allActions().size() == 4, "exactly four actions");
        CHECK(InputMap::typeOf(InputAction::Move) == InputActionType::Axis2D, "Move is axis2d");
        CHECK(InputMap::typeOf(InputAction::Look) == InputActionType::Axis2D, "Look is axis2d");
        CHECK(InputMap::typeOf(InputAction::Jump) == InputActionType::Button, "Jump is a button");
        CHECK(InputMap::typeOf(InputAction::Sprint) == InputActionType::Button, "Sprint is a button");
        CHECK(InputMap::isLatched(InputAction::Jump), "Jump is the latched action");
        CHECK(!InputMap::isLatched(InputAction::Sprint), "Sprint is held, not latched");
        CHECK(InputMap::usesMouse(InputAction::Look), "Look is the mouse action");
        CHECK(!InputMap::usesMouse(InputAction::Move), "Move is not mouse-driven");

        InputAction a;
        CHECK(InputMap::actionFromName("sprint", a) && a == InputAction::Sprint,
              "action names resolve case-insensitively");
        CHECK(!InputMap::actionFromName("Crouch", a), "an unknown action name is refused");
    }

    // ---- key names round-trip (the bare modifiers are the trap) -----------
    {
        CHECK(InputMap::keyName(Qt::Key_W) == "W", "Qt::Key_W spells W");
        CHECK(InputMap::keyName(Qt::Key_Space) == "Space", "Qt::Key_Space spells Space");
        CHECK(InputMap::keyName(Qt::Key_Shift) == "Shift",
              "a BARE modifier spells Shift, not QKeySequence's 'Shift+'");
        CHECK(InputMap::keyFromName("W") == Qt::Key_W, "W parses back");
        CHECK(InputMap::keyFromName("space") == Qt::Key_Space, "key names are case-insensitive");
        CHECK(InputMap::keyFromName("Shift") == Qt::Key_Shift, "Shift parses back");
        CHECK(InputMap::keyFromName("Left") == Qt::Key_Left, "an arrow key parses");
        CHECK(InputMap::keyFromName("") == 0 && InputMap::keyFromName("Nonsense") == 0,
              "an unparseable key name is 0, never a wrong key");
    }

    // ---- the shipped defaults (the owner's words: space to jump, W to walk)
    {
        InputMap map;
        const auto move = map.bindings(InputAction::Move);
        CHECK(move.size() == 4, "Move binds four keys by default");
        bool w = false, s = false, aa = false, d = false;
        for (const auto &b : move) {
            if (b.key == Qt::Key_W && near(b.x, 0) && near(b.y, +1)) w = true;
            if (b.key == Qt::Key_S && near(b.x, 0) && near(b.y, -1)) s = true;
            if (b.key == Qt::Key_A && near(b.x, -1) && near(b.y, 0)) aa = true;
            if (b.key == Qt::Key_D && near(b.x, +1) && near(b.y, 0)) d = true;
        }
        CHECK(w && s && aa && d, "W/S/A/D carry +Y/-Y/-X/+X");
        CHECK(map.bindings(InputAction::Jump).size() == 1 &&
              map.bindings(InputAction::Jump)[0].key == Qt::Key_Space, "Jump defaults to Space");
        CHECK(map.bindings(InputAction::Sprint).size() == 1 &&
              map.bindings(InputAction::Sprint)[0].key == Qt::Key_Shift, "Sprint defaults to Shift");
        CHECK(map.bindings(InputAction::Look).isEmpty(), "Look binds no keys — it is the mouse");
        CHECK(map.displayText(InputAction::Look) == "Mouse",
              "Look's Preferences row reads Mouse, not '(unbound)'");
        CHECK(map.displayText(InputAction::Move) == "W / S / A / D",
              "Move's Preferences row lists its keys");

        // The ShortcutOverride set (§8.3) is exactly the bound keys.
        CHECK(map.isBound(Qt::Key_W) && map.isBound(Qt::Key_Space) && map.isBound(Qt::Key_Shift),
              "the gameplay keys are bound");
        CHECK(!map.isBound(Qt::Key_E) && !map.isBound(Qt::Key_R) && !map.isBound(Qt::Key_F),
              "the gizmo keys E/R and focus F are NOT gameplay keys");
        bool found = false;
        CHECK(map.actionForKey(Qt::Key_A, &found) == InputAction::Move && found, "A belongs to Move");
        map.actionForKey(Qt::Key_G, &found);
        CHECK(!found, "an unbound key belongs to no action");
    }

    // ---- conflicts: named, and the refusal is ATOMIC ----------------------
    {
        InputMap map;
        QString action, key;
        const bool ok = map.bind(InputAction::Sprint, { { Qt::Key_W, 0, 0 } }, &action, &key);
        CHECK(!ok, "binding Sprint onto Move's W is refused");
        CHECK(action == "Move", "the conflict names the holding action");
        CHECK(key == "W", "the conflict names the key");
        CHECK(map.bindings(InputAction::Sprint).size() == 1 &&
              map.bindings(InputAction::Sprint)[0].key == Qt::Key_Shift,
              "the refused bind changed nothing");

        // Atomicity: a list whose SECOND key conflicts must not write the first.
        const bool ok2 = map.bind(InputAction::Sprint,
                                  { { Qt::Key_Control, 0, 0 }, { Qt::Key_D, 0, 0 } }, &action, &key);
        CHECK(!ok2 && key == "D", "a conflict anywhere in the list refuses the whole bind");
        CHECK(!map.isBound(Qt::Key_Control), "no partial write from the refused bind");

        // Rebinding a key INSIDE the same action is not a conflict.
        CHECK(map.bind(InputAction::Move, { { Qt::Key_W, 0, +1 }, { Qt::Key_S, 0, -1 } }),
              "shrinking Move's own list is allowed");
        CHECK(map.bindings(InputAction::Move).size() == 2, "Move now binds two keys");
        CHECK(!map.isBound(Qt::Key_A), "the dropped keys really went");
        // …and now Sprint CAN take A, because Move released it.
        CHECK(map.bind(InputAction::Sprint, { { Qt::Key_A, 0, 0 } }), "a freed key can be rebound");
    }

    // ---- binding round-trip: QVariantMap, then QSettings ------------------
    {
        InputMap map;
        map.bind(InputAction::Move, { { Qt::Key_Up, 0, +1 }, { Qt::Key_Down, 0, -1 },
                                      { Qt::Key_Left, -1, 0 }, { Qt::Key_Right, +1, 0 } });
        map.bind(InputAction::Jump, { { Qt::Key_Return, 0, 0 } });
        const QVariantMap serialized = map.toVariantMap();

        InputMap other;
        CHECK(other.fromVariantMap(serialized), "a serialized map is understood");
        CHECK(other.bindings(InputAction::Move).size() == 4, "the axis keys came back");
        bool up = false;
        for (const auto &b : other.bindings(InputAction::Move))
            if (b.key == Qt::Key_Up && near(b.y, +1) && near(b.x, 0)) up = true;
        CHECK(up, "an axis key's CONTRIBUTION round-trips, not just its key code");
        CHECK(other.bindings(InputAction::Jump).size() == 1 &&
              other.bindings(InputAction::Jump)[0].key == Qt::Key_Return, "the button key came back");
        CHECK(other.toVariantMap() == serialized, "the round trip is byte-stable");

        // Tolerance: a future action and a corrupt key must not brick the map.
        QVariantMap junk = serialized;
        junk.insert("Crouch", QStringList{ "C" });
        junk.insert("Jump", QStringList{ "NotAKey", "Return" });
        InputMap tolerant;
        CHECK(tolerant.fromVariantMap(junk), "an unknown action is skipped, not fatal");
        CHECK(tolerant.bindings(InputAction::Jump).size() == 1, "an unparseable key row is dropped");
        InputMap empty;
        CHECK(!empty.fromVariantMap(QVariantMap{ { "Crouch", QStringList{ "C" } } }),
              "a map with nothing recognizable reports false");
    }

    QTemporaryDir dir;
    const QString iniPath = dir.filePath("jahsettings.ini");
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        InputMap map;
        map.bind(InputAction::Jump, { { Qt::Key_Return, 0, 0 } });
        map.save(ini);
        ini.sync();
    }
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        InputMap map;
        map.load(ini);
        CHECK(map.bindings(InputAction::Jump).size() == 1 &&
              map.bindings(InputAction::Jump)[0].key == Qt::Key_Return,
              "bindings persist through jahsettings.ini");
        CHECK(map.bindings(InputAction::Move).size() == 4,
              "an action with a stored row does not disturb the others");
    }
    {
        // An action with NO stored row keeps its default rather than unbinding.
        QSettings ini(dir.filePath("empty.ini"), QSettings::IniFormat);
        InputMap map;
        map.load(ini);
        CHECK(map.bindings(InputAction::Move).size() == 4, "an empty settings file keeps defaults");
    }

    // ---- latch vs held: THE gate row --------------------------------------
    {
        InputSystem &sys = InputSystem::instance();
        sys.resetForTest();

        // HELD: the state is continuous for as long as the key is down. No
        // repeat events, no re-press — one press and the value stands.
        CHECK(sys.keyPressed(Qt::Key_W), "W is consumed by gameplay");
        CHECK(near(sys.state().move.y, 1.f) && near(sys.state().move.x, 0.f), "W walks forward");
        CHECK(near(sys.state().move.y, 1.f), "…and stays forward on a second read (held)");
        CHECK(!sys.keyPressed(Qt::Key_F), "an unbound key is NOT consumed");
        CHECK(near(sys.state().move.y, 1.f), "…and did not disturb the state");

        CHECK(sys.keyPressed(Qt::Key_D), "D is consumed");
        CHECK(near(sys.state().move.x, 0.7071f) && near(sys.state().move.y, 0.7071f),
              "a diagonal is clamped to the unit disc, not 1.41x fast");
        CHECK(sys.keyPressed(Qt::Key_A), "A is consumed");
        CHECK(near(sys.state().move.x, 0.f), "opposite keys cancel to a stop, not a drift");
        sys.keyReleased(Qt::Key_A);
        sys.keyReleased(Qt::Key_D);
        sys.keyReleased(Qt::Key_W);
        CHECK(sys.state().move.isZero(), "releasing every key stops the character");

        // Sprint is HELD too.
        sys.keyPressed(Qt::Key_Shift);
        CHECK(sys.state().sprint, "Shift sprints while held");
        sys.keyReleased(Qt::Key_Shift);
        CHECK(!sys.state().sprint, "…and stops on release");

        // LATCH: one press = one jump, consumed once, and a held key cannot
        // re-fire it (§5: "Cleared unconditionally after consumption so a held
        // key cannot re-fire").
        CHECK(!sys.state().jump, "no jump pending at rest");
        sys.keyPressed(Qt::Key_Space);
        CHECK(sys.state().jump, "Space latches a jump");
        CHECK(sys.consumeJump(), "the consumer gets the jump");
        CHECK(!sys.state().jump && !sys.consumeJump(), "the latch is cleared by consumption");
        sys.keyPressed(Qt::Key_Space);   // an unfiltered auto-repeat looks like this
        CHECK(!sys.state().jump, "a repeat press with no release in between cannot re-latch");
        sys.keyReleased(Qt::Key_Space);
        sys.keyPressed(Qt::Key_Space);
        CHECK(sys.state().jump, "a fresh press edge latches again");
        CHECK(sys.consumeJump() && !sys.consumeJump(), "…exactly once");
        sys.keyReleased(Qt::Key_Space);

        // Look accumulates and drains.
        sys.mouseMoved(3.f, -2.f);
        sys.mouseMoved(1.f, 1.f);
        CHECK(near(sys.state().look.x, 4.f) && near(sys.state().look.y, -1.f),
              "mouse deltas accumulate");
        const InputAxis2D look = sys.consumeLook();
        CHECK(near(look.x, 4.f) && sys.state().look.isZero(), "consumeLook drains the accumulator");

        // clearKeys is the focus-loss / stop path, and it is IDEMPOTENT (§8.3
        // rule 1: a redundant editor.stop() must cost nothing).
        sys.keyPressed(Qt::Key_W);
        sys.keyPressed(Qt::Key_Space);
        sys.clearKeys();
        CHECK(sys.state().move.isZero() && !sys.state().jump && !sys.state().sprint,
              "clearKeys drops the held set and the pending latch");
        sys.clearKeys();
        CHECK(sys.state().move.isZero(), "clearKeys twice is a no-op, not a fault");
        // …and the released-elsewhere key really is gone, not merely masked.
        CHECK(sys.keyReleased(Qt::Key_W) && sys.state().move.isZero(),
              "a release after clearKeys cannot resurrect motion");
    }

    // ---- a rebind changes what the producer does --------------------------
    {
        InputSystem &sys = InputSystem::instance();
        sys.resetForTest();
        CHECK(sys.bind(InputAction::Move, { { Qt::Key_Up, 0, +1 }, { Qt::Key_Down, 0, -1 } }),
              "Move rebound to the arrow keys");
        CHECK(!sys.keyPressed(Qt::Key_W), "W no longer belongs to gameplay");
        CHECK(sys.keyPressed(Qt::Key_Up) && near(sys.state().move.y, 1.f),
              "the new key walks forward");
        // A key held ACROSS a rebind must not leave a phantom contribution.
        sys.bind(InputAction::Move, { { Qt::Key_W, 0, +1 } });
        CHECK(sys.state().move.isZero(), "a rebind drops the orphaned held key's motion");
        sys.resetForTest();
    }

    // ---- the scripted producer (avatar.input's engine) --------------------
    {
        InputSystem &sys = InputSystem::instance();
        sys.resetForTest();
        sys.setMove(0.f, 1.f);
        CHECK(near(sys.state().move.y, 1.f), "a script can write move with no key events");
        sys.setMove(3.f, 4.f);
        CHECK(near(sys.state().move.x, 0.6f) && near(sys.state().move.y, 0.8f),
              "a script's move is clamped to the unit disc too");
        sys.setSprint(true);
        CHECK(sys.state().sprint, "a script can hold sprint");
        sys.requestJump();
        CHECK(sys.state().jump && sys.consumeJump() && !sys.consumeJump(),
              "a scripted jump is the same one-shot latch");
        // The keyboard producer wins the moment a real key arrives — documented,
        // and the reason a headless test must not mix the two.
        sys.setMove(0.f, 1.f);
        sys.keyPressed(Qt::Key_D);
        CHECK(near(sys.state().move.y, 0.f) && near(sys.state().move.x, 1.f),
              "a real key event recomputes move from the held set (last producer wins)");
        sys.resetForTest();
    }

    // ---- gameplayClaimsKey: the §8.3 predicate both viewports call --------
    {
        InputSystem::instance().resetForTest();
        CHECK(!gameplayClaimsKey(false, Qt::Key_W), "NOT playing: W belongs to the gizmo shortcut");
        CHECK(!gameplayClaimsKey(false, Qt::Key_Space), "NOT playing: Space belongs to tool.cycle");
        CHECK(gameplayClaimsKey(true, Qt::Key_W), "playing: W belongs to the character");
        CHECK(gameplayClaimsKey(true, Qt::Key_Space), "playing: Space belongs to the jump");
        CHECK(gameplayClaimsKey(true, Qt::Key_Shift), "playing: Shift belongs to the sprint");
        CHECK(!gameplayClaimsKey(true, Qt::Key_E),
              "playing: E is NOT claimed — an unbound key still reaches the editor");
        CHECK(!gameplayClaimsKey(true, Qt::Key_F11),
              "playing: fullscreen still works while the character walks");
    }

    std::printf(failures ? "FAILED (%d)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
