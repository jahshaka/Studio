#ifndef TOAST_H
#define TOAST_H

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

class Toast : QWidget
{
    Q_OBJECT

public:
    Toast(QWidget *parent = 0);
	void showToast(const QString &title, const QString &text, const float &delay);
	void showToast(const QString &title, const QString &text, const float &delay, const QPoint &pos, const QRect &rect);

private:
	QVBoxLayout *toastLayout;
	QLabel *caption;
	QLabel *info;
};

#endif // TOAST_H