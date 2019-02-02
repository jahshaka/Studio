#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QObject>

class PlayerView;

class PlayerWidget : public QWidget
{
	Q_OBJECT

	PlayerView* playerView;
public:
	explicit PlayerWidget(QWidget* parent = nullptr);
	~PlayerWidget() {}
	void createUI();
};

#endif PLAYERWIDGET_H