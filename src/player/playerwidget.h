#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QObject>
#include <QIcon>
#include "irisgl/irisglfwd.h"

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

	/// The page as a VIEW over the verbs (verb-coverage audit F1): whoever
	/// starts the player — this button, a script, an MCP session — the icon
	/// follows. Wired by the shell to PlayerService::playingChanged.
	void showPlaying(bool playing);

	/// PLAY, not "toggle play" (audit F4). Entering the player SPACE is not a
	/// button press: switchSpace(PLAYER) used to call onPlayScene(), so a script
	/// that had already started the scene had it STOPPED by the switch, and the
	/// second of two switches in a run left the page paused. The button keeps
	/// the toggle; the space entry states what it means.
	void playScene();

public slots:
    void onPlayScene();
};

#endif // PLAYERWIDGET_H
