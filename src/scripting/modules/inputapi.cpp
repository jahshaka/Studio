/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/inputapi.h"

#include "irisgl/document/input/inputmap.h"
#include "shell/mainwindow.h"

using iris::InputAction;
using iris::InputActionType;
using iris::InputKeyBinding;
using iris::InputMap;
using iris::InputSystem;

namespace {

QString typeName(InputAction a)
{
    return InputMap::typeOf(a) == InputActionType::Axis2D ? QStringLiteral("axis2d")
                                                          : QStringLiteral("button");
}

/// The four axis slots a script names when it rebinds an Axis2D action, and
/// the (x, y) each contributes. Deliberately named rather than positional:
/// `{up:"W", down:"S", left:"A", right:"D"}` reads the same in a script, in
/// the docs and in a settings file.
struct AxisSlot { const char *name; float x; float y; };
const AxisSlot kAxisSlots[] = {
    { "up",    0.f, +1.f },
    { "down",  0.f, -1.f },
    { "left", -1.f,  0.f },
    { "right", 1.f,  0.f },
};

QVariantMap axisMap(const iris::InputAxis2D &v)
{
    QVariantMap m;
    m.insert(QStringLiteral("x"), double(v.x));
    m.insert(QStringLiteral("y"), double(v.y));
    return m;
}

} // namespace

QVector<VerbInfo> InputApi::verbs() const
{
    return {
        { "bindings", "input.bindings() -> [{action, type, latched, mouse, keys, display}]",
          "The gameplay action map: Move and Look are axis2d, Jump and Sprint are buttons, and "
          "that set is CLOSED — there are no user-defined actions. `keys` lists the bound key "
          "names in binding order (Move ships with EIGHT: W/S/A/D and the arrow cluster, both "
          "spellings, because the editor's fly moved to the arrows and a hand arriving from there "
          "must not have to change grip to walk); `mouse` marks Look, whose "
          "primary producer is the mouse delta, so an empty key list there is the default rather "
          "than 'unbound'. `latched` marks Jump: it fires once per press and a held key cannot "
          "re-fire it.",
          Needs::Document },
        { "bind", "input.bind(action, binding) -> bool",
          "Rebinds one action and persists it (jahsettings.ini 'input/<Action>'). An axis takes "
          "{up, down, left, right} (any subset; the named slots keep their meaning) and REPLACES "
          "the action's whole key list — rebinding Move to WASD alone is how a user drops the "
          "shipped arrow keys — a button "
          "takes a key name or {key}. Key names are the portable spellings — 'W', 'Space', "
          "'Shift', 'Left'. Refused, changing nothing, when a key is already bound to a DIFFERENT "
          "action: the error names the action and the key. Pass null or {} to unbind.",
          Needs::Document },
        { "resetBindings", "input.resetBindings() -> bool",
          "Back to the shipped defaults — W/S/A/D AND the arrow keys move, mouse looks, Space "
          "jumps, Shift sprints. "
          "REMOVES the stored rows rather than writing the defaults into them, so a settings file "
          "that has been reset is byte-identical to one that was never rebound (the same contract "
          "the Shortcuts page's Reset All has).",
          Needs::Document },
        { "state", "input.state() -> {move:{x,y}, look:{x,y}, jump, sprint}",
          "The live input state: what the keyboard producer (or avatar.input) has put in it right "
          "now. `move` is the raw intent clamped to the unit disc, NOT yet camera-relative; `look` "
          "is the accumulated mouse delta since a consumer last drained it; `jump` is the pending "
          "latch. Read-only — write it with avatar.input.",
          Needs::Document },
    };
}

QVariantList InputApi::bindings()
{
    const InputMap &map = InputSystem::instance().map();
    QVariantList out;
    for (InputAction a : InputMap::allActions()) {
        QVariantMap row;
        row.insert(QStringLiteral("action"),  InputMap::actionName(a));
        row.insert(QStringLiteral("type"),    typeName(a));
        row.insert(QStringLiteral("latched"), InputMap::isLatched(a));
        row.insert(QStringLiteral("mouse"),   InputMap::usesMouse(a));
        QVariantList keys;
        for (const auto &b : map.bindings(a))
            keys.append(InputMap::keyName(b.key));
        row.insert(QStringLiteral("keys"), keys);
        row.insert(QStringLiteral("display"), map.displayText(a));
        out.append(row);
    }
    return out;
}

bool InputApi::bind(const QString &action, const QVariant &binding)
{
    InputAction a;
    if (!InputMap::actionFromName(action, a))
        return fail(QStringLiteral("input.bind: no such action '%1' — the set is Move, Look, "
                                   "Jump, Sprint and it is closed").arg(action));

    const bool axis = InputMap::typeOf(a) == InputActionType::Axis2D;
    QVector<InputKeyBinding> keys;

    if (!binding.isValid() || binding.isNull()) {
        // explicit unbind
    } else if (axis) {
        const QVariantMap m = binding.toMap();
        if (m.isEmpty() && !binding.toString().isEmpty())
            return fail(QStringLiteral("input.bind: %1 is an axis2d action — bind it with "
                                       "{up, down, left, right}, not a single key")
                            .arg(InputMap::actionName(a)));
        for (const auto &slot : kAxisSlots) {
            const QString name = m.value(QString::fromLatin1(slot.name)).toString();
            if (name.isEmpty()) continue;
            const int key = InputMap::keyFromName(name);
            if (key == 0)
                return fail(QStringLiteral("input.bind: '%1' names no key").arg(name));
            keys.append(InputKeyBinding{ key, slot.x, slot.y });
        }
    } else {
        QString name = binding.toString();
        if (name.isEmpty()) name = binding.toMap().value(QStringLiteral("key")).toString();
        if (!name.isEmpty()) {
            const int key = InputMap::keyFromName(name);
            if (key == 0)
                return fail(QStringLiteral("input.bind: '%1' names no key").arg(name));
            keys.append(InputKeyBinding{ key, 0.f, 0.f });
        }
    }

    QString conflictAction, conflictKey;
    if (!InputSystem::instance().bind(a, keys, &conflictAction, &conflictKey))
        return fail(QStringLiteral("input.bind: '%1' is already bound to %2 — unbind it there "
                                   "first").arg(conflictKey, conflictAction));
    refreshShortcutRows();
    return true;
}

bool InputApi::resetBindings()
{
    InputSystem::instance().resetBindings();
    refreshShortcutRows();
    return true;
}

QVariantMap InputApi::state()
{
    const iris::InputState &s = InputSystem::instance().state();
    QVariantMap m;
    m.insert(QStringLiteral("move"),   axisMap(s.move));
    m.insert(QStringLiteral("look"),   axisMap(s.look));
    m.insert(QStringLiteral("jump"),   s.jump);
    m.insert(QStringLiteral("sprint"), s.sprint);
    return m;
}

void InputApi::refreshShortcutRows()
{
    if (host.mainWindow) host.mainWindow->refreshGameplayShortcutRows();
}
