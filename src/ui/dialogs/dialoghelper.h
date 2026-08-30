#ifndef DIALOGHELPER_H
#define DIALOGHELPER_H

#include <QObject>

class DialogHelper : public QObject
{
    Q_OBJECT
public:
    explicit DialogHelper(QObject *parent = nullptr);
    static void centerDialog(QWidget* dialog);

signals:
};

#endif // DIALOGHELPER_H
