/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SLIDERLAYOUTMODEL_H
#define SLIDERLAYOUTMODEL_H

// Desktop SLIDER mode (DESKTOP_SLIDER_SPEC.md): the pure layout model behind
// the filmstrip rows. No widgets, no database — just guids, row assignments
// and per-row scroll offsets, so the seeding and insert-ordering rules are
// unit-testable headless. DynamicGrid owns one of these while the desktop is
// in Sliders mode; ProjectManager persists the {row, orderIndex} assignments
// it reports (per tile, in the projects table beside the freeform position).
//
// Rules encoded here (the spec's "lossless mode switching"):
//  - tiles with a stored {row, orderIndex} keep it (rows clamp into range,
//    order within a row is the stored index order, ties keep input order);
//  - unassigned tiles are seeded either round-robin in input order (input
//    order IS rows-mode ordering) or by freeform y-band (normY -> row,
//    ordered within the band by normX);
//  - moveTile is insert-at semantics: remove first, then insert at the index,
//    shifting the rest of the row right. Indices are implicit positions.
//  - per-row scroll offsets are session-only state (never persisted).

#include <QString>
#include <QVector>

struct SliderTileInfo
{
    QString guid;
    bool  hasSlider = false;    // stored {row, index} assignment exists
    int   row = 0;
    int   index = 0;
    bool  hasFreeform = false;  // stored freeform position exists (seed input)
    qreal normX = 0.0;
    qreal normY = 0.0;
};

class SliderLayoutModel
{
public:
    enum class Seed
    {
        RowsOrder,      // unassigned tiles: round-robin across rows, input order
        FreeformBands   // unassigned tiles with a freeform pos: y-band -> row
    };

    struct Pos
    {
        int row = -1;
        int index = -1;
        bool valid() const { return row >= 0 && index >= 0; }
    };

    void build(const QVector<SliderTileInfo> &tiles, int rowCount, Seed seed);
    void clear();

    int rowCount() const { return m_rows.size(); }
    const QVector<QVector<QString>> &rows() const { return m_rows; }
    int tileCount() const;

    Pos posOf(const QString &guid) const;

    // Insert-at semantics; index < 0 or past the end appends. A guid the model
    // has never seen is inserted too (used when a tile arrives mid-session).
    // Returns false only for an invalid guid or an empty model.
    bool moveTile(const QString &guid, int row, int index = -1);
    void removeTile(const QString &guid);

    // Session-only filmstrip scroll offsets (pixels; negative = slid left).
    qreal rowOffset(int row) const;
    void setRowOffset(int row, qreal offset);

private:
    QVector<QVector<QString>> m_rows;
    QVector<qreal> m_offsets;
};

#endif // SLIDERLAYOUTMODEL_H
