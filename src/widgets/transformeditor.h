#ifndef TRANSFORMEDITOR_H
#define TRANSFORMEDITOR_H

#include <QWidget>

namespace Ui {
class TransformEditor;
}

class TransformEditor : public QWidget
{
    Q_OBJECT

public:
    explicit TransformEditor(QWidget *parent = 0);
    ~TransformEditor();

private:
    Ui::TransformEditor *ui;
};

#endif // TRANSFORMEDITOR_H
