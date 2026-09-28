/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIBRARYMODEL_H
#define LIBRARYMODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QVector>

#include <functional>

/// ONE LIBRARY ROW as a listing carries it (D11-LIBRARY-SCALE): the columns of a
/// list query — never a thumbnail (the tile cache paints that by guid), never the
/// asset definition.
struct LibraryRow
{
    QString guid;
    QString name;
    int type = 0;
    int collection = 0;
    QString author;
    QString license;
    QStringList tags;
};

/// THE LIBRARY AS A MODEL (D11-LIBRARY-SCALE §3.2): the rows of one listing, the
/// guid lookup O(1), the tile a DecorationRole read from the session's TileCache
/// (a miss paints the placeholder and the row is refreshed by dataChanged when the
/// decode lands). The views are QListView (tiles) and QTreeView (the list mode's
/// three columns) over a LibraryFilterProxy each panel owns — no widget per asset.
///
/// ORDER: the listing's order, and a row added later (an import, a mint) goes to
/// the FRONT — the proxy sorts on OrderRole, descending, so an add is an append
/// here (the guid index stays valid) and never an O(n) shift.
class LibraryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Role {
        GuidRole = Qt::UserRole + 1,
        TypeRole,
        CollectionRole,
        NameRole,          // the stored name (DisplayRole is its base name)
        TagsRole,
        AuthorRole,
        LicenseRole,
        OrderRole,         // descending sort key: later adds first
        LoadingRole,       // this row's preview is loading (the tile's pulse)
        TileRole,          // the tile picture (the cache's, or the placeholder)
    };
    enum Column { NameColumn, TypeColumn, SizeColumn, ColumnCount };

    explicit LibraryModel(QObject *parent = nullptr);

    /// The tile size the DecorationRole answers at (the delegate's picture box).
    void setTileSize(const QSize &size);
    QSize tileSize() const { return mTileSize; }
    /// The Type column's words (the page owns the type names).
    void setTypeNamer(std::function<QString(int)> namer) { mTypeName = std::move(namer); }
    /// The Size column (the list mode reads them once per refresh).
    void setSizes(const QMap<QString, qint64> &sizes);
    /// The placeholder for a row whose tile is not (yet) there.
    void setPlaceholder(const QPixmap &placeholder) { mPlaceholder = placeholder; }
    /// Whether DecorationRole answers the tile (the default: a plain view shows
    /// it). Off for a model whose tiles only a LibraryTileDelegate paints (it
    /// reads TileRole): a view that shows no picture — the Library's list mode —
    /// must not make the cache fetch and decode one per row it lays out.
    void setDecorated(bool on) { mDecorated = on; }

    /// Replace every row (one reset).
    void setRows(const QVector<LibraryRow> &rows);
    /// Add a row at the FRONT, or refresh it in place when the guid is listed.
    void upsert(const LibraryRow &row);
    /// Drop the row of `guid`; false when it is not listed.
    bool remove(const QString &guid);
    /// Change the fields of a listed row (rename, tags, a drawer move).
    bool update(const QString &guid, const std::function<void(LibraryRow &)> &edit);
    /// The row's thumbnail changed: forget its tile and repaint it.
    void refreshTile(const QString &guid);
    /// Every row in `from` moves to drawer `to` (a drawer delete).
    void reassignCollections(const QVector<int> &from, int to);
    void setLoading(const QString &guid);

    int rowOfGuid(const QString &guid) const { return mIndex.value(guid, -1); }
    bool contains(const QString &guid) const { return mIndex.contains(guid); }
    const LibraryRow *rowFor(const QString &guid) const;
    const QVector<LibraryRow> &rows() const { return mRows; }
    QModelIndex indexOfGuid(const QString &guid, int column = 0) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;

private:
    void onTileReady(int kind, const QString &guid);
    void reindexFrom(int row);

    QVector<LibraryRow> mRows;
    QVector<int> mOrder;                // OrderRole per row
    QHash<QString, int> mIndex;         // guid -> row
    QMap<QString, qint64> mSizes;
    QSize mTileSize{ 128, 116 };
    QPixmap mPlaceholder;
    QString mLoadingGuid;
    bool mDecorated = true;
    int mNextOrder = 0;
    std::function<QString(int)> mTypeName;
};

/// One panel's view of a LibraryModel: a drawer, a search, a type set, a hidden
/// set — every filter AND-ed — sorted on OrderRole (later adds first).
class LibraryFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit LibraryFilterProxy(QObject *parent = nullptr);

    /// -1 = every drawer.
    void setCollection(int collection);
    int collection() const { return mCollection; }
    /// Case-insensitive, on the shown (base) name.
    void setSearch(const QString &text);
    /// Empty = every type.
    void setTypes(const QSet<int> &types);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    int mCollection = -1;
    QString mSearch;
    QSet<int> mTypes;
};

/// THE LIBRARY TILE, painted (the widget-per-asset AssetGridItem it replaces
/// cost three style sheets and two labels per asset): a raised card, the tile
/// picture (or the placeholder), the base name under it, the highlight when
/// selected or hovered, "Loading…" pulsing over the row whose preview loads.
class LibraryTileDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit LibraryTileDelegate(QObject *parent = nullptr);
    static constexpr int kTileWidth = 128;
    static constexpr int kTileHeight = 142;
    static constexpr int kPictureHeight = 116;

    void setPulse(bool phase) { mPulse = phase; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    bool mPulse = false;
};

#endif // LIBRARYMODEL_H
