#ifndef VRMANAGER_H
#define VRMANAGER_H

namespace iris{
class VrDevice;

class VrManager
{
    static VrDevice* device;
public:
    static VrDevice* getDefaultDevice();
};

}

#endif // VRMANAGER_H
