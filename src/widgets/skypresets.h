#ifndef SKYPRESETS_H
#define SKYPRESETS_H

#include <QWidget>

namespace Ui {
class SkyPresets;
}

class SkyPresets : public QWidget
{
    Q_OBJECT

public:
    explicit SkyPresets(QWidget *parent = 0);
    ~SkyPresets();

private:
    Ui::SkyPresets *ui;
};

#endif // SKYPRESETS_H
