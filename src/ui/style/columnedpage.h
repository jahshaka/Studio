/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef COLUMNEDPAGE_H
#define COLUMNEDPAGE_H

// A PAGE WITH COLUMNS (owner, 2026-09-11, smoke S1: "all right columns
// (Materials, Avatars, Assets) and left columns unify on the Editor's widths").
//
// Every full-window page in Studio is a work area with a column down one or
// both sides, and until S1 each page invented its own widths. They are all
// sized from ui/style/panelmetrics.h now — but a constant that a page forgets
// to use is invisible, so a page also SAYS which widgets its columns are. The
// shell reports them (app.columns) and the suites assert that what is on screen
// is what PanelMetrics says, per page, at the real laid-out width.
//
// A plain (non-QObject) interface: the pages are already QWidget subclasses and
// the shell cross-casts to this with dynamic_cast.

class QWidget;

class ColumnedPage
{
public:
    virtual ~ColumnedPage() = default;

    /// The page's left column widget, or null if it has none.
    virtual QWidget *leftColumn() const = 0;
    /// The page's right column widget, or null if it has none.
    virtual QWidget *rightColumn() const = 0;
};

#endif // COLUMNEDPAGE_H
