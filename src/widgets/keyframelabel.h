#ifndef KEYFRAMELABEL_H
#define KEYFRAMELABEL_H

#include <QWidget>
#include "../irisgl/src/irisglfwd.h"


namespace Ui {
class KeyFrameLabel;
}

class KeyFrameLabel : public QWidget
{
    Q_OBJECT

    bool collapsed;
    iris::FloatKeyFrame *keyFrame;

protected:
	void changeEvent(QEvent* event) override;
public:
    explicit KeyFrameLabel(QWidget *parent = 0);
    ~KeyFrameLabel();

    QList<KeyFrameLabel*> childLabels;

    void setTitle(QString title);

    void addChild(KeyFrameLabel* label);

    void collapse();
    void expand();

    void setKeyFrame(iris::FloatKeyFrame *value);

    iris::FloatKeyFrame *getKeyFrame() const;

protected slots:
    void toggleCollapse();

private:
    Ui::KeyFrameLabel *ui;
};

#endif // KEYFRAMELABEL_H
