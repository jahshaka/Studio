#ifndef PROGRESSBAR_H
#define PROGRESSBAR_H

#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QProgressbar>
#include <QPropertyAnimation>
#include <QPushButton>



class ProgressBar : public QProgressBar
{
    Q_OBJECT
    Q_PROPERTY(QPoint startPoint READ startPoint WRITE setStartPoint)


public:

    enum class Mode{
        Definite,
        Indefinite
    };


    ProgressBar(QWidget *parent = Q_NULLPTR);
    ~ProgressBar();
    QPoint startPoint();
    void setStartPoint(QPoint point);

    void setMode(Mode mode);
    void setTitle(QString string);
    void setValue(int val);
private:
    bool animating;
    bool showingCancelDialog;
    int offset=5;
    int _height, titleSize, confirmationSize;
    Mode mode;
    QPoint _startPoint;
    QGraphicsOpacityEffect *opacity;
    QLabel *title;
    QLabel *text;
    QWidget *contentHolder;
    QWidget *titleHolder;
    QPropertyAnimation *animator;
    QPushButton *close;
    QPushButton *confirm;
    QPushButton *cancel;


    void configureProgressBar(QWidget *parent = Q_NULLPTR);
    void configureConnections();
    void updateMode();
    void animate();
    void showConfirmationDialog();
private slots:


protected:
    void paintEvent(QPaintEvent *event);

signals:
    void modeChanged(Mode mode);
    void cancelOperations();
    void finished();
};

#endif // PROGRESSBAR_H
