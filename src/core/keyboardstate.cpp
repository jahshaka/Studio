/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
