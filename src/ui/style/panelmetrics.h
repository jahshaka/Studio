/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PANELMETRICS_H
#define PANELMETRICS_H

// THE RIGHT COLUMN IS ONE COLUMN (owner, 2026-09-08). The Properties dock and
// the Presets panel under it stack in the same column of the editor, so they
// are sized from the SAME number instead of from two literals that drifted
// apart (326 for the properties scroll area, 396 for the presets tab widget —
// which is why the properties side looked narrow beside the presets it sits
// above).
//
// `rightColumnWidth` is the column's DEFAULT width: what the shell gives the
// Properties dock in the default layout, and the width the Presets panel below
// asks for. A user resize wins for the rest of the session — this is a starting
// size, not a constraint.
//
// `rightColumnMinWidth` is the narrowest the column may be dragged. It is a
// real contract, not a hint: the properties content has NO horizontal scrollbar
// (mainwindow's scroll area sets Qt::ScrollBarAlwaysOff), so anything the rows
// cannot fit inside this width is CLIPPED and unreadable. ui.properties_width
// asserts the whole panel fits here, for every kind of selection.
namespace PanelMetrics {

constexpr int rightColumnWidth = 396;
constexpr int rightColumnMinWidth = 300;

/// The presets panel's own width, spelled separately only so the suite can
/// assert the two are the same column.
constexpr int presetsPanelWidth = rightColumnWidth;

// THE LEFT COLUMN IS ONE COLUMN TOO, AND IT IS THE SAME COLUMN ON EVERY PAGE
// (owner, 2026-09-11: "all right columns (Materials, Avatars, Assets) and left
// columns unify on the Editor's widths — the Editor right column is the correct
// width"). Before this every page carried its own literal — the Materials
// page's docks were 330 wide, the Assets page's metadata pane lived in a
// 280-380 band, the Avatar page's splitter opened at 220/800/280 — so moving
// between pages moved both edges of the work area. The four numbers below are
// the only widths any page's columns may be sized from.
//
// `leftColumnWidth` is what the shell gives the Hierarchy dock in the default
// editor layout, and what every other page's left column opens at;
// `leftColumnMinWidth` is the narrowest it may be dragged. A left column holds
// trees and lists, which elide rather than clip, so its minimum is a comfort
// limit, not the hard readability contract `rightColumnMinWidth` is.
constexpr int leftColumnWidth = 280;
constexpr int leftColumnMinWidth = 220;

}   // namespace PanelMetrics

#endif // PANELMETRICS_H
