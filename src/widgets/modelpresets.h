#ifndef MODELPRESETS_H
#define MODELPRESETS_H

#include <QWidget>

namespace Ui {
class ModelPresets;
}

class ModelPresets : public QWidget
{
    Q_OBJECT

public:
    explicit ModelPresets(QWidget *parent = 0);
    ~ModelPresets();

private:
    Ui::ModelPresets *ui;
};

#endif // MODELPRESETS_H
