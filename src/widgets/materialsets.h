#ifndef MATERIALSETS_H
#define MATERIALSETS_H

#include <QWidget>

namespace Ui {
class MaterialSets;
}

class MaterialSets : public QWidget
{
    Q_OBJECT

public:
    explicit MaterialSets(QWidget *parent = 0);
    ~MaterialSets();

private:
    Ui::MaterialSets *ui;
};

#endif // MATERIALSETS_H
