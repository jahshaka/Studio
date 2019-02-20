#ifndef HAND_GIZMO_H
#define HAND_GIZMO_H

enum class HandGesture
{
	Grab,
	Pinch
};

class HandGizmo
{
public:
	HandGesture gesture;
	float gesturFactor;


};


#endif // HAND_GIZMO_H