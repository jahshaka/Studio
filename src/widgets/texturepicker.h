#ifndef TEXTUREPICKER_H
#define TEXTUREPICKER_H

#include <QWidget>
#include <QLabel>

namespace Ui {
    class TexturePicker;
}

class TexturePicker : public QWidget
{
    Q_OBJECT

private slots:
    void changeTextureMap();
    void on_pushButton_clicked();

public:
    QString filename;

    explicit TexturePicker(QWidget *parent = 0);
    ~TexturePicker();
    Ui::TexturePicker *ui;

    void setLabelImage( QLabel* label, QString file, bool emitSignal = true);
    bool eventFilter(QObject *object, QEvent *ev);
    void setTexture(QString path);

signals:
    void valueChanged( QString value);

private:
    QString loadTexture();
};

#endif // TEXTUREPICKER_H
