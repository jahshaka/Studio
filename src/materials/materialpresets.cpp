/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "materialpresets.h"

QList<MaterialPreset> MaterialPreset::getDefaultPresets()
{
    QList<MaterialPreset> presets;

    //return presets;

    MaterialPreset mat;

    //abstract pattern
    mat = MaterialPreset();
    mat.name = "Abstract Art";
    mat.icon = "qrc:/app/materials/abstractpattern.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/Abstract Pattern.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Aged Brick Wall
    mat = MaterialPreset();
    mat.name = "Aged Brkwall";
    mat.icon = "qrc:/app/materials/agedbrickwall.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/Age Brick Wall.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Ancient Floor Brick
    mat = MaterialPreset();
    mat.name = "Floor Brick";
    mat.icon = "qrc:/app/materials/ancientfloorbrick.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "assets/textures/Ancient Floor Brick.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "app/content/textures/Ancient Floor Brick_SPEC.png";
    mat.shininess = 0;
    mat.normalTexture = "app/content/textures/Ancient Floor Brick_NRM.png";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Artistic Pattern
    mat = MaterialPreset();
    mat.name = "Art Pattern";
    mat.icon = "qrc:/app/materials/artisticpattern.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/Artistic Pattern.png";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Bamboo
    mat = MaterialPreset();
    mat.name = "Bamboo";
    mat.icon = "qrc:/app/materials/bamboo.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/bamboo.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "app/content/textures/bamboo_NRM.png";
    mat.normalIntensity = 0;
    mat.reflectionTexture="app/content/textures/bamboo_SPEC.png";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Bark Wood
    mat = MaterialPreset();
    mat.name = "Bark Wood";
    mat.icon = "qrc:/app/materials/barkwood.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/bark_wood.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "app/content/textures/bark_wood_NRM.jpg";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Brick Wall
    mat = MaterialPreset();
    mat.name = "Brick Wall";
    mat.icon = "qrc:/app/materials/brickwall.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/brick_wall.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "app/content/textures/brick_wall_SPEC.jpg";
    mat.shininess = 0;
    mat.normalTexture = "app/content/textures/brick_wall_NRM.jpg";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Chessboard
    mat = MaterialPreset();
    mat.name = "Chessboard";
    mat.icon = "qrc:/app/materials/chboard.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/chboard.png";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Floral
    mat = MaterialPreset();
    mat.name = "Floral Pattern";
    mat.icon = "qrc:/app/materials/floralpattern.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/floral.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Grass
    mat = MaterialPreset();
    mat.name = "Grass";
    mat.icon = "qrc:/app/materials/grass.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/Grass.png";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Green Pattern
    mat = MaterialPreset();
    mat.name = "Green Art";
    mat.icon = "qrc:/app/materials/greenpattern.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/Prawny Pattern.png";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Ground Board
    mat = MaterialPreset();
    mat.name = "Board";
    mat.icon = "qrc:/app/materials/groundboard.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/ground board.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Info Graphic
    mat = MaterialPreset();
    mat.name = "Info Graphic";
    mat.icon = "qrc:/app/materials/info_graphic.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/info_graphic.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Old Tile
    mat = MaterialPreset();
    mat.name = "Old Tile";
    mat.icon = "qrc:/app/materials/oldtile.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/old_tile.png";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Plaid
    mat = MaterialPreset();
    mat.name = "Plaid";
    mat.icon = "qrc:/app/materials/plaid.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/plaid.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Purple Swirl
    mat = MaterialPreset();
    mat.name = "Purple Swirl";
    mat.icon = "qrc:/app/materials/purpleswirls.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/Purple Swirls Pattern.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Red Brick Wall
    mat = MaterialPreset();
    mat.name = "Red Brkwall";
    mat.icon = "qrc:/app/materials/redbrickwall.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/Red Brick Wall.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "app/content/textures/Red Brick Wall_SPEC.jpg";
    mat.shininess = 0;
    mat.normalTexture = "app/content/textures/Red Brick Wall_NRM.jpg";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Rusted Iron
    mat = MaterialPreset();
    mat.name = "Rusted Iron";
    mat.icon = "qrc:/app/materials/rustediron.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/rusted iron.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 0;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //shiny red
    mat.name = "Shiny Red";
    mat.icon = "qrc:/app/materials/shiny_red.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,0,0);
    mat.diffuseTexture = "";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "";
    mat.shininess = 100;
    mat.normalTexture = "";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    //Wall Pattern
    mat = MaterialPreset();
    mat.name = "Stonewall";
    mat.icon = "qrc:/app/materials/wallpattern.png";
    mat.ambientColor = QColor(0,0,0);
    mat.diffuseColor = QColor(255,255,255);
    mat.diffuseTexture = "app/content/textures/wall-pattern.jpg";
    mat.specularColor = QColor(255,255,255);
    mat.specularTexture = "app/content/textures/wall-pattern_SPEC.jpg";
    mat.shininess = 0;
    mat.normalTexture = "app/content/textures/wall-pattern_NRM.jpg";
    mat.normalIntensity = 0;
    mat.reflectionTexture="";
    mat.reflectionInfluence = 0.0f;
    mat.textureScale = 1.0f;
    presets.push_back(mat);

    return presets;

}
