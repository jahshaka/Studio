/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef UI_CONTROLS_PENDINGTILES_H
#define UI_CONTROLS_PENDINGTILES_H

#include <QHash>
#include <QVector>

#include "data/project.h"

// THE DESKTOP's ROWS STILL WAITING FOR THEIR SLICE (ProjectManager's grid build, D11-LIBRARY-SCALE), in
// listing order, each found BY GUID in O(1) (LARGE-LIBRARY-1): the Desktop's entry check asks every
// project's guid, and a linear search made that O(P^2) at 500 projects. A removed row leaves an empty
// slot (its guid cleared) that `takeFirst` skips, so no slot ever moves and the index stays exact.
class PendingTiles
{
public:
    void reset(QVector<ProjectTileData> rows)
    {
        mRows = std::move(rows);
        mHead = 0;
        mIndex.clear();
        mIndex.reserve(mRows.size());
        for (int i = 0; i < mRows.size(); ++i) mIndex.insert(mRows.at(i).guid, i);
    }
    int size() const { return int(mIndex.size()); }
    bool isEmpty() const { return mIndex.isEmpty(); }
    bool contains(const QString &guid) const { return mIndex.contains(guid); }
    /// The waiting row of `guid`, or null.
    ProjectTileData *find(const QString &guid)
    {
        const auto it = mIndex.constFind(guid);
        return it == mIndex.cend() ? nullptr : &mRows[it.value()];
    }
    /// Drops `guid`'s waiting row; false when it was not waiting.
    bool remove(const QString &guid)
    {
        const auto it = mIndex.find(guid);
        if (it == mIndex.end()) return false;
        mRows[it.value()] = ProjectTileData();
        mIndex.erase(it);
        return true;
    }
    /// The first waiting row, in listing order. Not empty: the caller asks `isEmpty` first.
    ProjectTileData takeFirst()
    {
        while (mRows.at(mHead).guid.isEmpty()) ++mHead;
        ProjectTileData row = std::move(mRows[mHead++]);
        mIndex.remove(row.guid);
        if (mIndex.isEmpty()) reset({});
        return row;
    }

private:
    QVector<ProjectTileData> mRows;
    QHash<QString, int> mIndex;   // guid -> its slot in mRows
    int mHead = 0;                // the first slot not yet taken
};

#endif   // UI_CONTROLS_PENDINGTILES_H
