/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKIES_H
#define SKIES_H

#include <QEntity>
#include <Qt3DExtras/QSkyboxEntity>

/*
using namespace Qt3DCore;

enum class SkyType
{
    SkyBox,//currently not used
    SkySphere,
    SkyDome//currently not used
};

class Sky
{

};


class SkyDome:public Sky
{
    QEntity* entity;
public:
    SkyDome(QEntity* rootEnt)
    {
        entity = new QEntity(rootEnt);
    }

    void setTexture()
    {

    }
};

class FakeSkyBox:public Sky
{
    QEntity* entity;
public:
    SkyDome(QEntity* rootEnt)
    {
        entity = new QEntity(rootEnt);
    }

    void setTexture()
    {

    }
};
*/

#endif // SKIES_H
