#ifndef MODELPRESETS_H
#define MODELPRESETS_H

#include <QWidget>
#include <QModelIndex>

namespace Ui {
class ModelPresets;
}

class MainWindow;

class ModelPresets : public QWidget
{
    Q_OBJECT

public:
    explicit ModelPresets(QWidget *parent = 0);
    ~ModelPresets();

    void setMainWindow(MainWindow* mainWindow);

protected slots:
    void onPrimitiveSelected(QModelIndex itemIndex);

private:
    void addItem(QString name,QString path);

    MainWindow* mainWindow;
    Ui::ModelPresets *ui;
};

#endif // MODELPRESETS_H
