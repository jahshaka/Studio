/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIEWPORT_H
#define VIEWPORT_H

namespace iris
{

class Viewport
{
public:
    int x, y, width, height;
    int pixelRatioScale;

    Viewport() {
        x = 0;
        y = 0;
        width = 100;
        height = 100;
    }

    float getAspectRatio() {
        return (float) width / height;
    }
};

}
#endif // VIEWPORT_H
