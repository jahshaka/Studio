/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "vrmanager.h"
#include "vrdevice.h"

namespace iris{

VrDevice* VrManager::getDefaultDevice()
{
    if(device == nullptr)
        device = new VrDevice();

    return device;
}

VrDevice* VrManager::device = nullptr;


}
