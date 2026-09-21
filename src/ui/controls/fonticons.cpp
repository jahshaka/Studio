/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/fonticons.h"

#include "thirdparty/qtawesome/QtAwesome.h"

namespace fonticons {

QtAwesome &shared()
{
    // THE ONE `new QtAwesome` IN THE TREE (source.one_fonticons guards it).
    // Never deleted, on purpose — see the header.
    static QtAwesome *icons = [] {
        auto *set = new QtAwesome;
        set->initFontAwesome();
        return set;
    }();
    return *icons;
}

}   // namespace fonticons
