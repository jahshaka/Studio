#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QObject>
#include "irisglfwd.h"

class PlayerView;

class PlayerWidget : public QWidget
{
	Q_OBJECT

	PlayerView* playerView;
public:
	explicit PlayerWidget(QWidget* parent = nullptr);
	~PlayerWidget() {}
	void createUI();

	void setScene(iris::ScenePtr scene);
};

#endif PLAYERWIDGET_H