#include "keyboardstate.h"


QHash<int,bool> KeyboardState::keyStates = QHash<int,bool>();

/**
 * Returns true if the key is down, returns false otherwise
 * @param key
 * @return
 */
bool KeyboardState::isKeyDown(int key)
{
    if(!keyStates.contains(key))
        return false;

    return keyStates[key];
}

/**
 * Returns true if the key is up, returns false otherwise
 * @param key
 * @return
 */
bool KeyboardState::isKeyUp(int key)
{
    return !isKeyDown(key);
}

void KeyboardState::reset()
{
    keyStates = QHash<int,bool>();
}
