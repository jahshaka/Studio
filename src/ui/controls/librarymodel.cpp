/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/librarymodel.h"

#include <QApplication>
#include <QFileInfo>
#include <QLocale>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>

#include "ui/controls/assetdrag.h"
#include "ui/controls/tilecache.h"

// ---- LibraryModel -----------------------------------------------------------

LibraryModel::LibraryModel(QObject *parent) : QAbstractTableModel(parent)
{
    connect(&TileCache::instance(), &TileCache::tileReady, this,
            [this](TileCache::Kind kind, const QString &guid) { onTileReady(int(kind), guid); });
}

void LibraryModel::setTileSize(const QSize &size)
{
    if (size == mTileSize) return;
    mTileSize = size;
    if (!mRows.isEmpty())
        emit dataChanged(index(0, NameColumn), index(int(mRows.size()) - 1, NameColumn),
                         { Qt::DecorationRole, TileRole });
}

void LibraryModel::setSizes(const QMap<QString, qint64> &sizes)
{
    mSizes = sizes;
    if (!mRows.isEmpty())
        emit dataChanged(index(0, SizeColumn), index(int(mRows.size()) - 1, SizeColumn),
                         { Qt::DisplayRole });
}

void LibraryModel::setRows(const QVector<LibraryRow> &rows)
{
    beginResetModel();
    mRows = rows;
    mOrder.resize(mRows.size());
    // The listing's own order reads first-to-last: the first row carries the
    // highest key under the descending sort.
    const int n = int(mRows.size());
    for (int i = 0; i < n; ++i) mOrder[i] = n - i;
    mNextOrder = n + 1;
    mIndex.clear();
    mIndex.reserve(n);
    for (int i = 0; i < n; ++i) mIndex.insert(mRows.at(i).guid, i);
    endResetModel();
}

void LibraryModel::upsert(const LibraryRow &row)
{
    if (row.guid.isEmpty()) return;
    const int at = mIndex.value(row.guid, -1);
    if (at >= 0) {
        mRows[at] = row;
        emit dataChanged(index(at, 0), index(at, ColumnCount - 1));
        return;
    }
    const int n = int(mRows.size());
    beginInsertRows(QModelIndex(), n, n);
    mRows.append(row);
    mOrder.append(mNextOrder++);
    mIndex.insert(row.guid, n);
    endInsertRows();
}

bool LibraryModel::remove(const QString &guid)
{
    const int at = mIndex.value(guid, -1);
    if (at < 0) return false;
    beginRemoveRows(QModelIndex(), at, at);
    mRows.removeAt(at);
    mOrder.removeAt(at);
    mIndex.remove(guid);
    reindexFrom(at);
    endRemoveRows();
    if (mLoadingGuid == guid) mLoadingGuid.clear();
    return true;
}

bool LibraryModel::update(const QString &guid, const std::function<void(LibraryRow &)> &edit)
{
    const int at = mIndex.value(guid, -1);
    if (at < 0 || !edit) return false;
    edit(mRows[at]);
    emit dataChanged(index(at, 0), index(at, ColumnCount - 1));
    return true;
}

void LibraryModel::refreshTile(const QString &guid)
{
    TileCache::instance().invalidate(TileCache::Kind::Asset, guid);
    const int at = mIndex.value(guid, -1);
    if (at >= 0) emit dataChanged(index(at, NameColumn), index(at, NameColumn), { Qt::DecorationRole, TileRole });
}

void LibraryModel::reassignCollections(const QVector<int> &from, int to)
{
    // ONE RANGED dataChanged (the review, D11): one per row made a dynamic proxy
    // showing that drawer drop its rows one at a time — O(rows^2) for a big one.
    int first = -1, last = -1;
    for (int i = 0; i < mRows.size(); ++i) {
        if (!from.contains(mRows.at(i).collection)) continue;
        mRows[i].collection = to;
        if (first < 0) first = i;
        last = i;
    }
    // No role list: the drawer filter reads CollectionRole, and a proxy
    // re-filters only on the roles it is told about when told.
    if (first >= 0) emit dataChanged(index(first, 0), index(last, ColumnCount - 1));
}

void LibraryModel::setLoading(const QString &guid)
{
    const QString before = mLoadingGuid;
    mLoadingGuid = guid;
    for (const QString &g : { before, guid }) {
        const int at = mIndex.value(g, -1);
        if (at >= 0) emit dataChanged(index(at, NameColumn), index(at, NameColumn), { LoadingRole });
    }
}

const LibraryRow *LibraryModel::rowFor(const QString &guid) const
{
    const int at = mIndex.value(guid, -1);
    return at >= 0 ? &mRows.at(at) : nullptr;
}

QModelIndex LibraryModel::indexOfGuid(const QString &guid, int column) const
{
    const int at = mIndex.value(guid, -1);
    return at >= 0 ? index(at, column) : QModelIndex();
}

void LibraryModel::reindexFrom(int row)
{
    for (int i = row; i < mRows.size(); ++i) mIndex.insert(mRows.at(i).guid, i);
}

void LibraryModel::onTileReady(int kind, const QString &guid)
{
    if (kind != int(TileCache::Kind::Asset)) return;
    const int at = mIndex.value(guid, -1);
    if (at >= 0) emit dataChanged(index(at, NameColumn), index(at, NameColumn), { Qt::DecorationRole, TileRole });
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(mRows.size());
}

int LibraryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return QVariant();
    const LibraryRow &row = mRows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn: return QFileInfo(row.name).baseName();
        case TypeColumn: return mTypeName ? mTypeName(row.type) : QString::number(row.type);
        case SizeColumn: {
            const auto it = mSizes.constFind(row.guid);
            return it != mSizes.constEnd() ? QLocale().formattedDataSize(*it) : QStringLiteral("—");
        }
        }
        return QVariant();
    case Qt::ToolTipRole:
        return index.column() == NameColumn ? QVariant(row.name) : QVariant();
    case Qt::DecorationRole:
        if (!mDecorated) return QVariant();
        [[fallthrough]];
    case TileRole: {
        if (index.column() != NameColumn) return QVariant();
        // THE TILE BY GUID: a hit, or the placeholder while the cache fetches
        // and decodes it off this thread (dataChanged repaints it on arrival).
        const QPixmap tile = TileCache::instance().tile(TileCache::Kind::Asset, row.guid,
                                                        { mTileSize, 0 });
        return tile.isNull() ? mPlaceholder : tile;
    }
    case GuidRole: return row.guid;
    case TypeRole: return row.type;
    case CollectionRole: return row.collection;
    case NameRole: return row.name;
    case TagsRole: return row.tags;
    case AuthorRole: return row.author;
    case LicenseRole: return row.license;
    case OrderRole: return mOrder.at(index.row());
    case LoadingRole: return !mLoadingGuid.isEmpty() && row.guid == mLoadingGuid;
    default: return QVariant();
    }
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case NameColumn: return tr("Name");
    case TypeColumn: return tr("Type");
    case SizeColumn: return tr("Size");
    }
    return QVariant();
}

Qt::ItemFlags LibraryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren;
}

QStringList LibraryModel::mimeTypes() const
{
    return { QString::fromLatin1(AssetDrag::format()) };
}

QMimeData *LibraryModel::mimeData(const QModelIndexList &indexes) const
{
    // The drag an AssetGridItem started: one asset, the house payload
    // (ui/controls/assetdrag.h) — what every drop target already reads.
    for (const QModelIndex &index : indexes) {
        if (!index.isValid()) continue;
        const LibraryRow &row = mRows.at(index.row());
        return AssetDrag::mimeFor(row.type, row.name, QString(), row.guid,
                                  // The Assets page and the library picker are the LIBRARY (MATERIAL-DROP-1).
                                  AssetDrag::Origin::Library);
    }
    return nullptr;
}

Qt::DropActions LibraryModel::supportedDragActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

// ---- LibraryFilterProxy -----------------------------------------------------

LibraryFilterProxy::LibraryFilterProxy(QObject *parent) : QSortFilterProxyModel(parent)
{
    setSortRole(LibraryModel::OrderRole);
    setDynamicSortFilter(true);
    sort(0, Qt::DescendingOrder);
}

void LibraryFilterProxy::setCollection(int collection)
{
    if (collection == mCollection) return;
    beginFilterChange();
    mCollection = collection;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void LibraryFilterProxy::setSearch(const QString &text)
{
    const QString lowered = text.trimmed().toLower();
    if (lowered == mSearch) return;
    beginFilterChange();
    mSearch = lowered;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void LibraryFilterProxy::setTypes(const QSet<int> &types)
{
    if (types == mTypes) return;
    beginFilterChange();
    mTypes = types;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool LibraryFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (mCollection != -1 && index.data(LibraryModel::CollectionRole).toInt() != mCollection) return false;
    if (!mTypes.isEmpty() && !mTypes.contains(index.data(LibraryModel::TypeRole).toInt())) return false;
    if (!mSearch.isEmpty() && !index.data(Qt::DisplayRole).toString().toLower().contains(mSearch))
        return false;
    return true;
}

bool LibraryFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    return left.data(LibraryModel::OrderRole).toInt() < right.data(LibraryModel::OrderRole).toInt();
}

// ---- LibraryTileDelegate ----------------------------------------------------

LibraryTileDelegate::LibraryTileDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize LibraryTileDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return QSize(kTileWidth, kTileHeight);
}

void LibraryTileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect card = option.rect.adjusted(1, 1, -1, -1);
    const QPalette &pal = option.palette;
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);

    // The card: the palette's raised surface, a touch darker on hover, the
    // highlight colour when selected (the old tile's #3498db came from the
    // theme's accent; the palette's Highlight IS that accent).
    QColor base = pal.color(QPalette::Button);
    if (hovered && !selected) base = base.darker(115);
    QPainterPath path;
    path.addRoundedRect(QRectF(card), 3, 3);
    painter->fillPath(path, base);
    if (selected) {
        painter->setPen(QPen(pal.color(QPalette::Highlight), 2));
        painter->drawPath(path);
    }

    // The picture, centred in its box.
    const QRect box(card.left(), card.top(), card.width(), kPictureHeight - 2);
    const QPixmap tile = index.data(LibraryModel::TileRole).value<QPixmap>();
    if (!tile.isNull()) {
        const QSize fit = tile.size().scaled(box.size(), Qt::KeepAspectRatio).boundedTo(tile.size());
        const QRect at(box.left() + (box.width() - fit.width()) / 2,
                       box.top() + (box.height() - fit.height()) / 2, fit.width(), fit.height());
        painter->drawPixmap(at, tile);
    }

    // The name: two lines at most, centred, elided.
    const QRect text(card.left() + 4, box.bottom() + 1, card.width() - 8, card.bottom() - box.bottom() - 1);
    painter->setPen(pal.color(selected ? QPalette::HighlightedText : QPalette::ButtonText));
    const QString name = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                       Qt::ElideRight, text.width() * 2 - 8);
    painter->drawText(text, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, name);

    // The double-click's loading pulse (ASSET_DRAWERS_SPEC §1).
    if (index.data(LibraryModel::LoadingRole).toBool()) {
        QColor scrim = pal.color(QPalette::Window);
        scrim.setAlpha(170);
        painter->fillPath(path, scrim);
        painter->setPen(mPulse ? pal.color(QPalette::Highlight) : pal.color(QPalette::WindowText));
        painter->drawText(card, Qt::AlignCenter, tr("Loading…"));
    }
    painter->restore();
}
