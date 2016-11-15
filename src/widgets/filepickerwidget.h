#ifndef FILEPICKERWIDGET_H
#define FILEPICKERWIDGET_H

#include <QWidget>
#include <QLabel>

namespace Ui {
class FilePickerWidget;
}

class FilePickerWidget : public QWidget
{
    Q_OBJECT

private slots:

void filePicker();

public:

    QString filename;
    QString filepath;
    QString file_extentions;

    explicit FilePickerWidget( QWidget *parent = 0);
    ~FilePickerWidget();
    Ui::FilePickerWidget *ui;

private:

    QString loadTexture();

};

#endif // FILEPICKERWIDGET_H
