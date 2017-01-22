/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
