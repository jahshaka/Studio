#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QObject>
#include <QIcon>
#include <functional>
#include "irisgl/irisglfwd.h"

class EnginePlayerView;
class QPushButton;

class PlayerWidget : public QWidget
{
	Q_OBJECT

	EnginePlayerView* playerView;
	QPushButton* playBtn = nullptr;
	QPushButton* vrBtn = nullptr;
	QIcon playIcon, stopIcon;
	std::function<void()> vrToggle;
public:
	/// The widget takes ownership of `view`.
	explicit PlayerWidget(QWidget* parent = nullptr, EnginePlayerView* view = nullptr);
	/// The shell calls this ahead of removeScene(): the Player's VR session
	/// must end before the engine scene it is bound to goes (see
	/// EnginePlayerView::endVrForSceneClose). A no-op without a session.
	void endVrForSceneClose();
	~PlayerWidget() {}
	void createUI();

	void setScene(iris::ScenePtr scene);

	/// Enters the page (EnginePlayerView::start). FALSE with `why` filled when
	/// the player cannot draw — nothing was started and the shell should stay
	/// where it was (SMOKE-FIX-1).
	bool begin(QString *why = nullptr);
	void end();

	/// The page as a VIEW over the verbs (verb-coverage audit F1): whoever
	/// starts the player — this button, a script, an MCP session — the icon
	/// follows. Wired by the shell to PlayerService::playingChanged.
	void showPlaying(bool playing);

	/// THE PLAYER PAGE'S VR BUTTON (SPECS/VR_SPEC.md §4.5, phase 3). The shell
	/// hands it the same call the editor toolbar's icon makes — one
	/// implementation, in PlayerService — and `showVr` keeps the button
	/// honest: disabled when this process cannot do VR at all (with the reason
	/// in the tooltip), pressed while a session runs.
	void setVrToggle(const std::function<void()> &toggle, const QIcon &icon);
	void showVr(bool available, bool active);

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
