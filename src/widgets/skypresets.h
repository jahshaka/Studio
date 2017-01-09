#ifndef SKYPRESETS_H
#define SKYPRESETS_H

#include <QWidget>

namespace Ui {
class SkyPresets;
}

class QListWidgetItem;
class MainWindow;

class SkyPresets : public QWidget
{
    Q_OBJECT

public:
    explicit SkyPresets(QWidget *parent = 0);
    ~SkyPresets();

    void addSky(QString path, QString name);


    void setMainWindow(MainWindow* mainWindow)
    {
        this->mainWindow = mainWindow;
    }

protected slots:
    void applySky(QListWidgetItem* item);

private:
    Ui::SkyPresets *ui;
    QList<QString> skies;
    MainWindow* mainWindow;
};

#endif // SKYPRESETS_H
