#ifndef VIEWPORT_H
#define VIEWPORT_H

namespace iris
{

class Viewport
{
public:
    int x,y;
    int width,height;

    Viewport()
    {
        x = 0;y =0;
        width=100;height=100;
    }

    float getAspectRatio()
    {
        return (float)width/height;
    }
};

}
#endif // VIEWPORT_H
