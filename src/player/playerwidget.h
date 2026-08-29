#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>
#include <QObject>
#include <QIcon>
#include "irisglfwd.h"

class IPlayerView;
class QPushButton;

class PlayerWidget : public QWidget
{
	Q_OBJECT

	IPlayerView* playerView;
	QPushButton* playBtn;
	QIcon playIcon, stopIcon;
public:
	/// With no `view` the legacy PlayerView (IrisGL, own GL context) is created.
	/// Engine mode passes an EnginePlayerView; the widget takes ownership either way.
	explicit PlayerWidget(QWidget* parent = nullptr, IPlayerView* view = nullptr);
	~PlayerWidget() {}
	void createUI();

	void setScene(iris::ScenePtr scene);

	void begin();
	void end();

public slots:
    void onPlayScene();
};

#endif // PLAYERWIDGET_H
