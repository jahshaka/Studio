#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QObject>
#include <QIcon>
#include "irisglfwd.h"

class EnginePlayerView;
class QPushButton;

class PlayerWidget : public QWidget
{
	Q_OBJECT

	EnginePlayerView* playerView;
	QPushButton* playBtn;
	QIcon playIcon, stopIcon;
public:
	/// The widget takes ownership of `view`.
	explicit PlayerWidget(QWidget* parent = nullptr, EnginePlayerView* view = nullptr);
	~PlayerWidget() {}
	void createUI();

	void setScene(iris::ScenePtr scene);

	void begin();
	void end();

public slots:
    void onPlayScene();
};

#endif // PLAYERWIDGET_H
