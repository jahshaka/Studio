#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QObject>
#include <QIcon>
#include "irisglfwd.h"

class PlayerView;
class QPushButton;

class PlayerWidget : public QWidget
{
	Q_OBJECT

	PlayerView* playerView;
	QPushButton* playBtn;
	QIcon playIcon, stopIcon;
public:
	explicit PlayerWidget(QWidget* parent = nullptr);
	~PlayerWidget() {}
	void createUI();

	void setScene(iris::ScenePtr scene);

	void begin();
	void end();

public slots:
    void onPlayScene();
};

#endif PLAYERWIDGET_H
