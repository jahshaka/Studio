#ifndef LOADER_H
#define LOADER_H

#include <QDialog>
#include <QLabel>

class Loader : public QDialog
{
    Q_OBJECT
    Q_PROPERTY(qreal length READ length WRITE setLength NOTIFY lengthChanged)
    Q_PROPERTY(qreal startAngle READ startAngle WRITE setStartAngle NOTIFY angleChanged)
public:
    Loader(QWidget *parent = Q_NULLPTR);
    void setText(QString string);
    void stop();

    qreal length();
    void setLength(qreal val);

    qreal startAngle();
    void setStartAngle(qreal val);

private :
    QLabel *label;
    void setConnections();
    qreal _length, _startAngle;
    int offset = 20;

signals:
    void loaded( bool b);
    void loading();
    void lengthChanged();
    void angleChanged();

public slots:
    void startAnimation();
    void reverseAnimation();
    void forwardAnimation();
protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // LOADER_H
