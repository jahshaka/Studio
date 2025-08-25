#include "dialoghelper.h"

#include <QApplication>
#include <QWidget>
#include <QScreen>

DialogHelper::DialogHelper(QObject *parent)
    : QObject{parent}
{}

void DialogHelper::centerDialog(QWidget *dialog)
{
    if (!dialog)
        return;

    QWidget* parent = dialog->parentWidget();
    QRect parentRect;
    if (parent) {
        parentRect = parent->geometry();
    } else {
        parentRect = QApplication::primaryScreen()->geometry();
    }

    QSize dlgSize = dialog->size();
    int x = parentRect.x() + (parentRect.width() - dlgSize.width()) / 2;
    int y = parentRect.y() + (parentRect.height() - dlgSize.height()) / 2;

    dialog->move(x, y);
}
