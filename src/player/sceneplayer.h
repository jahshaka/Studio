#pragma once

enum class ScenePlayerState
{
	Play,
	Pause,
	Stop
};

// plays scene
class ScenePlayer
{
	ScenePlayerState state;
public:
	ScenePlayer() {}
	void setScene(iris::ScenePtr scene) {}

	// captures transform state of each object before start playing
	// gets active cutscene and sets it up for playing
	void play() {}

	// resets objects to state before playing
	void stop() {}

	void pause() {}

	void update(float dt) {}
};