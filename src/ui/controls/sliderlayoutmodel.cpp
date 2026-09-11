/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/sliderlayoutmodel.h"

#include <algorithm>

void SliderLayoutModel::build(const QVector<SliderTileInfo> &tiles, int rowCount, Seed seed)
{
    rowCount = qMax(1, rowCount);

    m_rows.clear();
    m_rows.resize(rowCount);

    // offsets are session state: keep what rows survive a rebuild, zero the rest
    if (m_offsets.size() != rowCount) {
        QVector<qreal> offsets(rowCount, 0.0);
        for (int i = 0; i < qMin(m_offsets.size(), offsets.size()); ++i)
            offsets[i] = m_offsets[i];
        m_offsets = offsets;
    }

    // pass 1 — stored assignments win. Sort each row by the stored orderIndex;
    // std::stable_sort keeps input order for equal indices, so a legacy store
    // with duplicate indices still yields a deterministic strip.
    struct Stored { int index; int arrival; QString guid; };
    QVector<QVector<Stored>> stored(rowCount);
    QVector<const SliderTileInfo*> unassigned;

    int arrival = 0;
    for (const SliderTileInfo &tile : tiles) {
        if (tile.guid.isEmpty()) continue;
        if (tile.hasSlider) {
            const int row = qBound(0, tile.row, rowCount - 1);
            stored[row].push_back({ tile.index, arrival, tile.guid });
        } else {
            unassigned.push_back(&tile);
        }
        ++arrival;
    }

    for (int r = 0; r < rowCount; ++r) {
        std::stable_sort(stored[r].begin(), stored[r].end(),
                         [](const Stored &a, const Stored &b) { return a.index < b.index; });
        for (const Stored &s : stored[r]) m_rows[r].push_back(s.guid);
    }

    // pass 2 — seed the rest (DESKTOP_SLIDER_SPEC "lossless mode switching")
    if (seed == Seed::FreeformBands) {
        // y-band -> row, ordered within the band by x; tiles that never got a
        // freeform position fall through to round-robin below
        struct Banded { qreal normX; int arrival; const SliderTileInfo *tile; };
        QVector<QVector<Banded>> bands(rowCount);
        QVector<const SliderTileInfo*> leftovers;
        int i = 0;
        for (const SliderTileInfo *tile : unassigned) {
            if (tile->hasFreeform) {
                const int row = qBound(0, int(tile->normY * rowCount), rowCount - 1);
                bands[row].push_back({ tile->normX, i, tile });
            } else {
                leftovers.push_back(tile);
            }
            ++i;
        }
        for (int r = 0; r < rowCount; ++r) {
            std::stable_sort(bands[r].begin(), bands[r].end(),
                             [](const Banded &a, const Banded &b) { return a.normX < b.normX; });
            for (const Banded &b : bands[r]) m_rows[r].push_back(b.tile->guid);
        }
        unassigned = leftovers;
    }

    // NEW TILES COME IN AT THE TOP LEFT (owner, 2026-09-12: "new projects on the
    // desktop should always come in at the top left and push the rest over").
    // When the desktop already HAS an arrangement (any stored slot), a tile with
    // no slot is new — a project just created or imported — so it goes to the
    // FRONT of the first row, in input order (the rows ordering is newest first),
    // and that row's tiles move one place right. Only a desktop with NO stored
    // arrangement at all (the very first seed) is dealt round-robin.
    bool anyStored = false;
    for (int r = 0; r < rowCount && !anyStored; ++r) anyStored = !stored[r].isEmpty();
    if (seed == Seed::RowsOrder && anyStored) {
        int at = 0;
        for (const SliderTileInfo *tile : unassigned) m_rows[0].insert(at++, tile->guid);
        unassigned.clear();
    }

    // round-robin fill in input order (input order IS the rows-mode ordering)
    int next = 0;
    for (const SliderTileInfo *tile : unassigned) {
        m_rows[next % rowCount].push_back(tile->guid);
        ++next;
    }
}

void SliderLayoutModel::clear()
{
    m_rows.clear();
    m_offsets.clear();
}

int SliderLayoutModel::tileCount() const
{
    int count = 0;
    for (const auto &row : m_rows) count += row.size();
    return count;
}

SliderLayoutModel::Pos SliderLayoutModel::posOf(const QString &guid) const
{
    for (int r = 0; r < m_rows.size(); ++r) {
        const int i = m_rows[r].indexOf(guid);
        if (i >= 0) return { r, i };
    }
    return {};
}

bool SliderLayoutModel::moveTile(const QString &guid, int row, int index)
{
    if (guid.isEmpty() || m_rows.isEmpty()) return false;

    removeTile(guid);

    row = qBound(0, row, m_rows.size() - 1);
    if (index < 0 || index > m_rows[row].size()) index = m_rows[row].size();
    m_rows[row].insert(index, guid);
    return true;
}

void SliderLayoutModel::removeTile(const QString &guid)
{
    for (auto &row : m_rows) {
        const int i = row.indexOf(guid);
        if (i >= 0) { row.removeAt(i); return; }
    }
}

qreal SliderLayoutModel::rowOffset(int row) const
{
    return (row >= 0 && row < m_offsets.size()) ? m_offsets[row] : 0.0;
}

void SliderLayoutModel::setRowOffset(int row, qreal offset)
{
    if (row >= 0 && row < m_offsets.size()) m_offsets[row] = offset;
}
