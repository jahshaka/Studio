#ifndef BLOOMPOSTPROCESS_H
#define BLOOMPOSTPROCESS_H

#include "../irisglfwd.h"
#include "../graphics/postprocess.h"

namespace iris
{

class PostProcessContext;

class BloomPostProcess : public PostProcess
{
public:

    PostProcessPass* hBlurPass;
    PostProcessPass* vBlurPass;
    PostProcessPass* combinePass;

    BloomPostProcess()
    {
        HBlurPass
    }

    virtual void process(PostProcessContext* context) override
    {

    }

};

}

#endif // BLOOMPOSTPROCESS_H
