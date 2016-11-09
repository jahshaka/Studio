/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialwidget.h"
#include "ui_materialwidget.h"
//#include "qmaterial.h"
//#include "../scenegraph/scenenodes.h"
#include "../jah3d/core/scenenode.h"
#include "qfiledialog.h"

MaterialWidget::MaterialWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MaterialWidget)
{
    ui->setupUi(this);

    /*
     * List Widget for Materials
     * */

    ui->listWidget->setViewMode(QListWidget::IconMode);
    ui->listWidget->setIconSize(QSize(70,70));
    ui->listWidget->setResizeMode(QListWidget::Adjust);
    ui->listWidget->setMovement(QListView::Static);
    ui->listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    /*
    auto presets = MaterialPreset::getDefaultPresets();
    for(auto preset:presets)
    {
        this->addPreset(preset);
    }
    */

    /*
    connect(ui->listWidget,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(materialPresetChanged(QListWidgetItem*)));


    //texture map changes
    connect(ui->diffuseMap,SIGNAL(pressed()),this,SLOT(changeDiffuseMap()));
    connect(ui->specularMap,SIGNAL(pressed()),this,SLOT(changeSpecularMap()));
    connect(ui->normalMap,SIGNAL(pressed()),this,SLOT(changeNormalMap()));
    connect(ui->reflectionMap,SIGNAL(pressed()),this,SLOT(changeReflectionMap()));

    connect(ui->diffuseColor,SIGNAL(onSetColor(QColor)),this,SLOT(setDiffuse(QColor)));
    connect(ui->specColor,SIGNAL(onSetColor(QColor)),this,SLOT(setSpecular(QColor)));
    connect(ui->ambientColor,SIGNAL(onSetColor(QColor)),this,SLOT(setAmbient(QColor)));

    connect(ui->normalIntensity,SIGNAL(valueChanged(int)),this,SLOT(normalIntensityChanged(int)));
    connect(ui->shininess,SIGNAL(valueChanged(int)),this,SLOT(shininessChanged(int)));
    connect(ui->reflectionFactor,SIGNAL(valueChanged(int)),this,SLOT(reflectionFactorChanged(int)));

    connect(ui->clearDiffuseMap,SIGNAL(pressed()),this,SLOT(clearDiffuseMap()));
    connect(ui->clearNormalMap,SIGNAL(pressed()),this,SLOT(clearNormalMap()));
    connect(ui->clearSpecularMap,SIGNAL(pressed()),this,SLOT(clearSpecularMap()));
    connect(ui->clearReflectionMap,SIGNAL(pressed()),this,SLOT(clearReflectionMap()));
    */

    ui->textureScale->setLabelText("");
    connect(ui->textureScale,SIGNAL(valueChanged(int)),this,SLOT(changeTextureScale(int)));
    ui->textureScale->setValueRange(0,1000);

    matProxy = nullptr;
    //node = nullptr;
}

MaterialWidget::~MaterialWidget()
{
    delete ui;
}
/*
void MaterialWidget::normalIntensityChanged(int val)
{
    float shininess = val/100.0f;
    material->setNormalIntensity(shininess);
}


void MaterialWidget::shininessChanged(int val)
{
    float shininess = val/100.0f;
    material->setShininess(shininess);
}

void MaterialWidget::alphaChanged(int val)
{
    Q_UNUSED(val);
    //float alpha = val/100.0f;
}

void MaterialWidget::reflectionFactorChanged(int val)
{
    float influence = val/10000.0f;
    material->setReflectionInfluence(influence);
}

void MaterialWidget::setSceneNode(SceneNode* node)
{
    this->node = node;
    this->material = node->mat;

    //update ui
    updateUI(node->mat);
}

void MaterialWidget::setMaterial(AdvanceMaterial* mat)
{
    node->setMaterial(mat);
    this->material = mat;
}

void MaterialWidget::updateUI(AdvanceMaterial* material)
{
    ui->specColor->setColor(material->getSpecularColor());
    ui->diffuseColor->setColor(material->getDiffuseColor());
    ui->ambientColor->setColor(material->getAmbientColor());

    //shininess and alpha
    ui->shininess->setValue(material->getShininess()*100);
    ui->normalIntensity->setValue(material->getNormalIntensity()*100);
    ui->reflectionFactor->setValue(material->getReflectionInfluence()*10000);
    //ui->alpha->setValue(data->alpha);

    //textures
    normalTex = material->getNormalTextureSource();
    setLabelImage(this->ui->normalLabel,normalTex);

    auto diffuseTex = material->getDiffuseTextureSource();
    setLabelImage(this->ui->diffuseLabel,diffuseTex);

    specularTex = material->getSpecularTextureSource();
    setLabelImage(this->ui->specLabel,specularTex);

    auto reflTex = material->getReflectionTextureSource();
    setLabelImage(this->ui->reflectionLabel,reflTex);

    ui->textureScale->setValue(material->getTextureScale()*100);

}

void MaterialWidget::setDiffuseMap(QString path)
{
    material->setDiffuseTexture(TextureHelper::loadTexture(path));
}

void MaterialWidget::setNormalMap(QString path)
{
    material->setNormalTexture(TextureHelper::loadTexture(path));
}

void MaterialWidget::setSpecularMap(QString path)
{
    material->setSpecularTexture(TextureHelper::loadTexture(path));
}

void MaterialWidget::setReflectionMap(QString path)
{
    material->setReflectionTexture(TextureHelper::loadTexture(path));
}


void MaterialWidget::setAmbient(QColor col)
{
    material->setAmbientColor(col);
}

void MaterialWidget::setDiffuse(QColor col)
{
    material->setDiffuseColor(col);
}

void MaterialWidget::setSpecular(QColor col)
{
    material->setSpecularColor(col);
}


//slots
void MaterialWidget::changeDiffuseMap()
{
    auto file = loadTexture();
    if(file.isEmpty() || file.isNull())
        return;

    this->setLabelImage(ui->diffuseLabel,file);
    setDiffuseMap(file);
}


void MaterialWidget::changeNormalMap()
{
    auto file = loadTexture();
    if(file.isEmpty() || file.isNull())
        return;


    this->setLabelImage(ui->normalLabel,file);
    setNormalMap(file);
}

void MaterialWidget::changeSpecularMap()
{
    auto file = loadTexture();
    if(file.isEmpty() || file.isNull())
        return;


    this->setLabelImage(ui->specLabel,file);
    setSpecularMap(file);
}

void MaterialWidget::changeReflectionMap()
{
    auto file = loadTexture();
    if(file.isEmpty() || file.isNull())
        return;

    this->setLabelImage(ui->reflectionLabel,file);
    setReflectionMap(file);
}

void MaterialWidget::clearDiffuseMap()
{
    this->material->removeDiffuseTexture();
    clearLabelImage(ui->diffuseLabel);
}

void MaterialWidget::clearSpecularMap()
{
    this->material->removeSpecularTexture();
    clearLabelImage(ui->specLabel);
}

void MaterialWidget::clearNormalMap()
{
    this->material->removeNormalTexture();
    clearLabelImage(ui->normalLabel);
}

void MaterialWidget::clearReflectionMap()
{
    this->material->removeReflectionTexture();
    clearLabelImage(ui->reflectionLabel);
}

void MaterialWidget::changeTextureScale(int val)
{
    this->material->setTextureScale(val/100.0f);
}

void MaterialWidget::setLabelImage(QLabel* label,QString file)
{
    if(file.isNull() || file.isEmpty())
    {
        label->clear();
    }
    else
    {
        QImage image;
        image.load(file);

        label->setPixmap(QPixmap::fromImage(image));
    }
}

void MaterialWidget::clearLabelImage(QLabel* label)
{
    label->clear();
}

void MaterialWidget::addPreset(MaterialPreset preset)
{
    auto item = new QListWidgetItem(QIcon(preset.icon),preset.name);
    item->setData(Qt::UserRole,QVariant::fromValue(preset.name));
    ui->listWidget->addItem(item);

    presets.insert(preset.name,preset);
}

void MaterialWidget::materialPresetChanged(QListWidgetItem* item)
{

    auto presetName = item->data(Qt::UserRole).value<QString>();
    auto preset = presets[presetName];

    material->applyPreset(preset);

    //todo: apply preset
    updateUI(material);
}

QString MaterialWidget::loadTexture()
{
    QString dir = QApplication::applicationDirPath()+"/assets/textures/";
    return QFileDialog::getOpenFileName(this,"Open Texture File",dir,"Image Files (*.png *.jpg *.bmp)");
}
*/

