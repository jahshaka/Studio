/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETPANEL_H
#define ASSETPANEL_H

#include <QListWidget>
#include <QWidget>

#include <QLineEdit>
#include <QPainter>
#include <QApplication>
#include <QStyledItemDelegate>

#include "core/project.h"
#include "core/database/database.h"
#include "mainwindow.h"

struct DefaultModel
{
    QString objectName;
    QString meshPath;
    QString objectIcon;
};

class FMListViewDelegate : public QStyledItemDelegate
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
        result.setHeight(84);
        result.setWidth(84);
        return result;
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
        const QModelIndex &index) const override
    {
        if (index.data().canConvert<QString>()) {
            QLineEdit *editor = new QLineEdit(parent);
            connect(editor, &QLineEdit::editingFinished, this, &FMListViewDelegate::commitAndCloseEditor);
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

class AssetPanel : public QWidget
{
public:
    explicit AssetPanel(QWidget *parent = 0) : QWidget(parent) {
        mainWindow = nullptr;

        listView = new QListWidget;
        listView->setViewMode(QListWidget::IconMode);
        listView->setIconSize(QSize(64, 64));
        listView->setResizeMode(QListWidget::Adjust);
        listView->setMovement(QListView::Static);
        listView->setSelectionBehavior(QAbstractItemView::SelectItems);
        listView->setSelectionMode(QAbstractItemView::SingleSelection);
        listView->setSpacing(4);

        listView->setAttribute(Qt::WA_MacShowFocusRect, false);
        listView->setAttribute(Qt::WA_MacShowFocusRect, false);
    }

    ~AssetPanel() {}

    void populateFavorites() {
        favoriteAssets = handle->fetchFavorites();
    }

    virtual void setMainWindow(MainWindow *window) = 0;
    virtual void setDatabaseHandle(Database *handle) = 0;
    virtual void addNewItem(QListWidgetItem *item) = 0;
    virtual void removeFavorite(const QString &guid) = 0;
    virtual void addFavorites() = 0;

protected:
    QVector<AssetRecord> favoriteAssets;
    QListWidget *listView;
    MainWindow *mainWindow;
    Database *handle;
};

#endif