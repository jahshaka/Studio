#ifndef POSTPROCESSMANAGER_H
#define POSTPROCESSMANAGER_H

#include "../irisglfwd.h"

namespace iris {

class PostProcess;

class PostProcessManager
{
    QList<PostProcess*> postProcesses;

public:
    PostProcessManager()
    {

    }

    void apply();
};
}

#endif // POSTPROCESSMANAGER_H
