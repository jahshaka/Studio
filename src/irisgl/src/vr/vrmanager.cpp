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
