/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETFAVORITES_H
#define ASSETFAVORITES_H

#include <QWidget>
#include <QListWidget>
#include <QListWidgetItem>

#include "core/project.h"
#include "core/database/database.h"

class MainWindow;

#include <QLineEdit>
#include <QPainter>
#include <QApplication>
#include <QStyledItemDelegate>

class FListViewDelegate : public QStyledItemDelegate
{
protected:
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        //        painter->save();
        QPalette::ColorRole textRole = QPalette::NoRole;

        if (option.state & QStyle::State_Selected) {
            textRole = QPalette::HighlightedText;
            painter->fillRect(option.rect, QColor(70, 70, 70, 255));
        }

        if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(option.rect, QColor(64, 64, 64));
        }

        // handle selection
        //if (option.state & QStyle::State_Selected) {
        //	//          painter->save();
        //	textRole = QPalette::Mid;
        //	//          QBrush selectionBrush(QColor(128, 128, 128, 128));
        //	//          painter->setBrush(selectionBrush);
        //	//          painter->drawRect(r.adjusted(1, 1,-1,-1));
        //	//          painter->restore();
        //}

        painter->setRenderHint(QPainter::Antialiasing);


        auto opt = option;
        initStyleOption(&opt, index);
        QRect r = opt.rect;

        QFontMetrics metrix(painter->font());
        int width = r.width() - 8;
        QString clippedText = metrix.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, width);

        QString title = clippedText;
        //        QString description = index.data(Qt::UserRole + 1).toString();

        QPalette::ColorGroup cg = opt.state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
        if (cg == QPalette::Normal && !(opt.state & QStyle::State_Active))
            cg = QPalette::Inactive;

        // set pen color
        if (opt.state & QStyle::State_Selected)
            painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
        else
            painter->setPen(opt.palette.color(cg, QPalette::Text));


        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        //        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        QIcon ic = QIcon(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)));
        r = option.rect.adjusted(0, 0, 0, 0);
        style->drawItemPixmap(painter, r, Qt::AlignCenter, ic.pixmap(QSize(64, 64)));

        //r = option.rect.adjusted(50, 0, 0, -50);
        //        painter->drawText(r.left(), r.top(), r.width(), r.height(),
        //                          Qt::AlignBottom|Qt::AlignCenter|Qt::TextWordWrap, title, &r);
        style->drawItemText(painter, opt.rect, Qt::AlignBottom | Qt::AlignCenter | Qt::TextSingleLine,
            opt.palette, true, title, textRole);


        //        painter->restore();
        //r = option.rect.adjusted(50, 50, 0, 0);
        //        painter->drawText(r.left(), r.top(), r.width(), r.height(), Qt::AlignLeft|Qt::TextWordWrap, description, &r);
        //        auto opt = option;
        //        initStyleOption(&opt, index);

        //        QString line0 = index.model()->data(index.model()->index(index.row(), 0)).toString();
        //        QString line1 = index.model()->data(index.model()->index(index.row(), 2)).toString();

        //        // draw correct background
        //        opt.text = "";


        //        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);


        //        painter->drawText(QRect(rect.left(), rect.height(), rect.width(), rect.height()), opt.displayAlignment, line0);
    }

    QSize sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const
    {
        QSize result = QStyledItemDelegate::sizeHint(option, index);
        result.setHeight(100);
        result.setWidth(100);
        return result;
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        if (index.data().canConvert<QString>()) {
            QLineEdit *editor = new QLineEdit(parent);
            connect(editor, &QLineEdit::editingFinished, this, &FListViewDelegate::commitAndCloseEditor);
            return editor;
        }
        else {
            return QStyledItemDelegate::createEditor(parent, option, index);
        }
    }

    void setEditorData(QWidget *editor,
        const QModelIndex &index) const
    {
        if (index.data().canConvert<QString>()) {
            const QString text = index.data().toString();
            QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor);
            //lineEdit->setMaxLength(15);
            lineEdit->setText(text);
        }
        else {
            QStyledItemDelegate::setEditorData(editor, index);
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
        const QModelIndex &index) const
    {
        if (index.data().canConvert<QString>()) {
            QLineEdit *textEditor = qobject_cast<QLineEdit *>(editor);
            model->setData(index, QVariant::fromValue(qobject_cast<QLineEdit*>(textEditor)->text()));
        }
        else {
            QStyledItemDelegate::setModelData(editor, model, index);
        }
    }

    void commitAndCloseEditor()
    {
        QLineEdit *editor = qobject_cast<QLineEdit*>(sender());
        emit commitData(editor);
        emit closeEditor(editor);
    }
};

class AssetFavorites : public QWidget
{
    Q_OBJECT

public:
    explicit AssetFavorites(QWidget *parent = 0);
    ~AssetFavorites();

    void setMainWindow(MainWindow* mainWindow) {
        this->mainWindow = mainWindow;
    }

    void setHandle(Database *db) {
        this->handle = db;

         // extra
        favoriteAssets = handle->fetchFavorites();
        populateFavorites();
    }

    void addFavorite(const QListWidgetItem *item);
    void removeFavorite(const QString &assetGuid);

    void populateFavorites();

    bool eventFilter(QObject *watched, QEvent *event);

public slots:
    void showContextMenu(const QPoint &pos);

private:
    QVector<AssetRecord> favoriteAssets;
    QListWidget *listView;
    MainWindow *mainWindow;
    Database *handle;

    QPoint startPos;
};

#endif // MATERIALSETS_H
