#ifndef KEYFRAMELABEL_H
#define KEYFRAMELABEL_H

#include <QWidget>


namespace Ui {
class KeyFrameLabel;
}

class KeyFrameLabel : public QWidget
{
    Q_OBJECT

    bool collapsed;

public:
    explicit KeyFrameLabel(QWidget *parent = 0);
    ~KeyFrameLabel();

    QList<KeyFrameLabel*> childLabels;

    void setTitle(QString title);

    void addChild(KeyFrameLabel* label);

    void collapse();
    void expand();

protected slots:
    void toggleCollapse();

private:
    Ui::KeyFrameLabel *ui;
};

#endif // KEYFRAMELABEL_H
