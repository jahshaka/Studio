#ifndef KEYBOARDSTATE_H
#define KEYBOARDSTATE_H

#include <QHash>

/**
 * This classes is a representation of the state of the keyboard
 */
class KeyboardState
{
public:
    // this stores the states of the keys
    // a key state is true when a key is down
    // and false when a key is up
    static QHash<int,bool> keyStates;

    static bool isKeyDown(int key);

    static bool isKeyUp(int key);

    static void reset();
};

#endif // KEYBOARDSTATE_H
