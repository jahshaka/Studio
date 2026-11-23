// THE KEY NAMES (NOQT-DOCUMENT-1 stage 5, the ruling's call 4): the document keeps a binding's key as an int (a Qt::Key
// value) and stores it as one; the EDITOR spells it - "W", "Space", "Shift" - for the Preferences rows, the input verbs
// and a conflict's message. QtGui's QKeySequence portable text, with a table for the bare modifiers: QKeySequence spells
// a bare modifier "Shift+" / "Ctrl+" (it reads the value as a modifier with no key), which does not round-trip, and
// Sprint's default is one.
#pragma once

#include "irisgl/document/input/inputmap.h"

#include <QKeySequence>
#include <QString>
#include <QStringList>

namespace keynames {

static_assert(iris::inputkeys::Space == Qt::Key_Space && iris::inputkeys::A == Qt::Key_A && iris::inputkeys::D == Qt::Key_D
                  && iris::inputkeys::S == Qt::Key_S && iris::inputkeys::W == Qt::Key_W
                  && iris::inputkeys::Left == Qt::Key_Left && iris::inputkeys::Up == Qt::Key_Up
                  && iris::inputkeys::Right == Qt::Key_Right && iris::inputkeys::Down == Qt::Key_Down
                  && iris::inputkeys::Shift == Qt::Key_Shift,
              "the document's default key codes are Qt::Key's");

struct NamedKey { int key = 0; const char *name = nullptr; };
inline constexpr NamedKey kNamedKeys[] = {
    { Qt::Key_Shift,   "Shift"   },
    { Qt::Key_Control, "Ctrl"    },
    { Qt::Key_Alt,     "Alt"     },
    { Qt::Key_Meta,    "Meta"    },
    { Qt::Key_Space,   "Space"   },
};

/// "W", "Space", "Shift", … Round-trips with keyFromName. Empty for 0.
inline QString keyName(int key)
{
    if (key == 0) return QString();
    for (const auto &nk : kNamedKeys)
        if (nk.key == key) return QString::fromLatin1(nk.name);
    return QKeySequence(key).toString(QKeySequence::PortableText);
}

/// 0 when `name` names no key.
inline int keyFromName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return 0;
    for (const auto &nk : kNamedKeys)
        if (trimmed.compare(QLatin1String(nk.name), Qt::CaseInsensitive) == 0) return nk.key;
    const QKeySequence seq = QKeySequence::fromString(trimmed, QKeySequence::PortableText);
    if (seq.count() != 1) return 0;
    // Strip the modifier bits: a gameplay binding is one physical key.
    const int key = seq[0].toCombined() & ~Qt::KeyboardModifierMask;
    // fromString does NOT fail on garbage — "Nonsense" comes back as a one-element sequence holding Qt::Key_unknown
    // (0x01ffffff). Without this line every unparseable name would bind a real, unreachable key code.
    if (key == Qt::Key_unknown) return 0;
    return key;
}

/// A human display string for one action ("W / S / A / D", "Mouse", "Space") — what the Preferences fixed rows show.
inline QString displayText(const iris::InputMap &map, iris::InputAction a)
{
    QStringList names;
    for (const auto &b : map.bindings(a)) {
        const QString n = keyName(b.key);
        if (!n.isEmpty()) names << n;
    }
    if (iris::InputMap::usesMouse(a)) names.prepend(QStringLiteral("Mouse"));
    if (names.isEmpty()) return QStringLiteral("(unbound)");
    return names.join(QStringLiteral(" / "));
}

}   // namespace keynames
