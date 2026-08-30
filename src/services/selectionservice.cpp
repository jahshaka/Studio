/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/selectionservice.h"

void SelectionService::select(iris::SceneNodePtr node)
{
    if (mInSelect) return;
    struct Guard {
        bool &flag;
        Guard(bool &f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(mInSelect);

    mSelected = node;
    emit selectionChanged(node);
}
