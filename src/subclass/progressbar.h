#ifndef PROGRESSBAR_H
#define PROGRESSBAR_H

#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressbar>
#include <QPropertyAnimation>
#include <QPushButton>



class ProgressBar : public QProgressBar
{
    Q_OBJECT
    Q_PROPERTY(QPoint startPoint READ startPoint WRITE setStartPoint)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)


public:

    enum class Mode{
        Definite,
        Indefinite
    };


    ProgressBar(QWidget *parent = Q_NULLPTR);
    ~ProgressBar();

    QPoint startPoint();
    void setStartPoint(QPoint point);

    QColor backgroundColor();
    void setBackgroundColor(QColor color);

    void setMode(Mode mode);
    void setTitle(QString string);
    void setValue(int val);
    void setUnit(int unit);
    void showConfirmationDialog();
    void setConfirmationText(QString string);
    void setConfirmationButtons(QString confirm, QString cancel, bool showConfirm = true, bool showCancel = true);
    void showConfirmButton(bool showConfirm);
    void showCancelButton( bool showCancel );
private:
    bool animating;
    bool showingCancelDialog;
    bool isPressed;
    int offset=5;
    int _height, titleSize, confirmationSize;
    QColor _backgroundColor;
    Mode mode;
    QPoint _startPoint;
    QGraphicsOpacityEffect *opacity;
    QLabel *title;
    QLabel *text;
    QWidget *contentHolder;
    QWidget *titleHolder;
    QPropertyAnimation *animator;
    QPushButton *closeBtn;
    QPushButton *confirm;
    QPushButton *cancel;
    QString confirmationText;
    QPoint oldPos;

    void configureProgressBar(QWidget *parent = Q_NULLPTR);
    void configureConnections();
    void updateMode();
    void animate();
private slots:


protected:
    void paintEvent(QPaintEvent *event);
    void mouseMoveEvent(QEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event) ;
    void mouseReleaseEvent(QMouseEvent *event);
signals:
    void modeChanged(Mode mode);
    void cancelOperations();
    void finished();
    void backgroundColorChanged(QColor color);
};

#endif // PROGRESSBAR_H
