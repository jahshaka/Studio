/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FONTICONS_H
#define FONTICONS_H

// THE FONT-AWESOME ICON SET, ONCE PER PROCESS (QTAWESOME-1, UNINIT_SWEEP_SPEC
// §7).
//
// WHAT A QtAwesome INSTANCE COSTS. The class is a 2-phase construction: the
// constructor makes an icon painter and five default options, and
// `initFontAwesome()` loads the font (once per process — the font id is a
// function-local static in the vendored TU) and then fills THIS INSTANCE'S
// `namedCodepoints_` with 786 name → codepoint entries. That hash is per
// object, so every instance pays for it, and nothing in this tree ever
// deleted one: the shell's, the materials page's, and — worst — a fresh one
// per BasePropertyWidget, i.e. one 786-entry hash leaked per property row the
// materials page built.
//
// SO THERE IS ONE, AND THIS IS IT. The shell already treated its instance as
// the process's (player/playerwidget.cpp: "THE ICON COMES FROM THE SHELL,
// which owns the QtAwesome font set (one instance per process). The
// alternative — a second QtAwesome here — would load the font twice for one
// glyph"). That sentence is now true by construction rather than by care.
//
// DELIBERATELY LEAKED, like the three function-local statics ENGINE-3 records:
// the instance is a QObject with no parent and it is alive for as long as any
// widget can ask it for an icon, which is until the last widget is gone — an
// order this cannot be sure of at static-destruction time, and a dangling icon
// painter is a crash where a few hundred kilobytes is not.
//
// FIRST CALL MUST BE ON THE UI THREAD AND AFTER QApplication EXISTS
// (QFontDatabase's requirement, not ours). Every caller is a widget's
// constructor.

class QtAwesome;

namespace fonticons {

/// The process's icon set, font loaded and named codepoints filled.
QtAwesome &shared();

}   // namespace fonticons

#endif   // FONTICONS_H
