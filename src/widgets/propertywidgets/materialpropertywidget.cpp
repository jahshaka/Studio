/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../accordianbladewidget.h"

#include "materialpropertywidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"
#include "../texturepickerwidget.h"
#include "../labelwidget.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/materials/custommaterial.h"

#include "../../io/materialreader.hpp"

void MaterialPropertyWidget::createWidgets(const QJsonObject &jahShader)
{
    /// TODO: add some check here to escape early
    auto widgetProps = jahShader["uniforms"].toArray();

//    sliderUniforms.clear();
//    colorUniforms.clear();
//    textureUniforms.clear();

//    qDebug() << widgetProps.size();

    /// TODO see if this can be removed this when the default material is deleted.
    unsigned sliderSize, textureSize, colorSize;
    sliderSize = textureSize = colorSize = 0;

    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop = widgetProps[i].toObject();
        if (prop["type"] == "slider")   sliderSize++;
        if (prop["type"] == "color")    colorSize++;
        if (prop["type"] == "texture")  textureSize++;
    }

    for (int propIndex = 0; propIndex < widgetProps.size(); propIndex++) {
        auto prop = widgetProps[propIndex].toObject();

        auto name       = prop["name"].toString();
        auto uniform    = prop["uniform"].toString();

        if (sliderUniforms.size() < sliderSize) {
            if (prop["type"] == "slider") {
                auto start  = (float) prop["start"].toDouble();
                auto end    = (float) prop["end"].toDouble();

                sliderUniforms.push_back(iris::make_mat_struct(sliderUniforms.size() - 1,
                                                               uniform,
                                                               addFloatValueSlider(name, start, end)));
                sliderUniforms.back().value->index = sliderUniforms.size() - 1;
            }
        }

        if (colorUniforms.size() < colorSize) {
            if (prop["type"] == "color") {
                colorUniforms.push_back(
                            iris::make_mat_struct(colorUniforms.size() - 1,
                                                  uniform,
                                                  addColorPicker(name)));
                colorUniforms.back().value->index = colorUniforms.size() - 1;
                colorUniforms.back().value->getPicker()->index = colorUniforms.size() - 1;
            }
        }

        if (textureUniforms.size() < textureSize) {
            if (prop["type"] == "texture") {
                textureUniforms.push_back(
                            iris::make_mat_struct(textureUniforms.size() - 1,
                                                  uniform,
                                                  addTexturePicker(name)));
                textureUniforms.back().value->index = textureUniforms.size() - 1;
            }
        }
    }
}

void MaterialPropertyWidget::setupCustomMaterial()
{
    createWidgets(this->customMaterial->getShaderFile());

    // sliders
    QSignalMapper *signalMapper = new QSignalMapper(this);

    auto sit = sliderUniforms.begin();
    while (sit != sliderUniforms.end()) {
        connect(sit->value, SIGNAL(valueChanged(float)), signalMapper, SLOT(map()));
        signalMapper->setMapping(sit->value, sit->value);
        ++sit;
    }

    connect(signalMapper, SIGNAL(mapped(QWidget*)), SLOT(onCustomSliderChanged(QWidget*)));

    // colors
    QSignalMapper *signalMapper2 = new QSignalMapper(this);

    auto ccit = colorUniforms.begin();
    while (ccit != colorUniforms.end()) {
        connect(ccit->value->getPicker(), SIGNAL(onColorChanged(QColor)), signalMapper2, SLOT(map()));
        signalMapper2->setMapping(ccit->value->getPicker(), ccit->value->getPicker());
        ++ccit;
    }

    connect(signalMapper2, SIGNAL(mapped(QWidget*)), SLOT(onCustomColorChanged(QWidget*)));

    // textures
    QSignalMapper *signalMapper21 = new QSignalMapper(this);

    auto ccit2 = textureUniforms.begin();
    while (ccit2 != textureUniforms.end()) {
        connect(ccit2->value, SIGNAL(valueChanged(QString)), signalMapper21, SLOT(map()));
        signalMapper21->setMapping(ccit2->value, ccit2->value);
        ++ccit2;
    }

    connect(signalMapper21, SIGNAL(mapped(QWidget*)), SLOT(onCustomTextureChanged(QWidget*)));

    // iterate both (different typed) maps in lockstep...
    // we get the value from the material and set the widget value
    // this works because the maps are ordered by key
    // TODO do this up at signal mapper boi
    // the order is not necessarilly right ok...
    // SLIDERS
    auto mat = sliderUniforms.begin();
    auto mit = customMaterial->sliderUniforms.begin();
    while (mat != sliderUniforms.end()) {
        mat->value->setValue(mit->value);
        ++mat;
        ++mit;
    }

    // COLORS
    auto cat = colorUniforms.begin();
    auto mit2 = customMaterial->colorUniforms.begin();
    while (cat != colorUniforms.end()) {
        cat->value->setColorValue(mit2->value);
        ++cat;
        ++mit2;
    }

    // TExTURES
    auto tat = textureUniforms.begin();
    auto mit22 = customMaterial->textureUniforms.begin();
    while (tat != textureUniforms.end()) {
        tat->value->setTexture(mit22->value);
        ++tat;
        ++mit22;
    }
}

void MaterialPropertyWidget::setupShaderSelector()
{
    // TODO load these from a directory in the future...
    materialSelector = this->addComboBox("Shader");

    QDir dir(IrisUtils::getAbsoluteAssetPath("app/shader_defs/"));
    for (auto shaderName : dir.entryList(QDir::Files)) {
        materialSelector->addItem(QFileInfo(shaderName).baseName());
    }

    materialSelector->setCurrentItem(this->customMaterial->name);

    connect(materialSelector,   SIGNAL(currentIndexChanged(QString)),
            this,               SLOT(onMaterialSelectorChanged(QString)));
}

MaterialPropertyWidget::MaterialPropertyWidget(QWidget *parent)
{

}

void MaterialPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
        this->customMaterial = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
    }

    setupShaderSelector();

    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        materialReader = new MaterialReader();
        materialReader->readJahShader(IrisUtils::getAbsoluteAssetPath("app/shader_defs/" + this->customMaterial->name + ".json"));
        this->customMaterial->generate(materialReader->getParsedShader());
        setupCustomMaterial();
    } else {
        this->meshNode.clear();
        this->customMaterial.clear();
        return;
    }
}

void MaterialPropertyWidget::onCustomSliderChanged(QWidget *t)
{
    auto changedIndex = dynamic_cast<HFloatSliderWidget*>(t);
    if (!!customMaterial) {
        customMaterial->sliderUniforms[changedIndex->index].value = changedIndex->getValue();
    }
}

void MaterialPropertyWidget::onCustomColorChanged(QWidget *t)
{
    auto changedIndex = dynamic_cast<ColorPickerWidget*>(t);
    if (!!customMaterial) {
        customMaterial->colorUniforms[changedIndex->index].value = changedIndex->getColor();
    }
}

void MaterialPropertyWidget::onCustomTextureChanged(QWidget *t)
{
    auto changedIndex = dynamic_cast<TexturePickerWidget*>(t);

    if (!!customMaterial) {
        // TODO smarten this up a bit, doesn't need to a be an if
        if (changedIndex->getTexturePath().isEmpty() || changedIndex->getTexturePath().isNull()) {
            customMaterial->textureToggleUniforms[changedIndex->index].value = false;
            customMaterial->textureUniforms[changedIndex->index].value = "";
            customMaterial->setTextureWithUniform(textureUniforms[changedIndex->index].uniform, "");
        } else {
            customMaterial->textureToggleUniforms[changedIndex->index].value = true;
            customMaterial->textureUniforms[changedIndex->index].value = changedIndex->getTexturePath();
            customMaterial->setTextureWithUniform(textureUniforms[changedIndex->index].uniform,
                                                  changedIndex->getTexturePath());
        }
    }
}

void MaterialPropertyWidget::purge()
{
    sliderUniforms.clear();
    colorUniforms.clear();
    textureUniforms.clear();
}

void MaterialPropertyWidget::onMaterialSelectorChanged(const QString &text)
{
    // only clear when we are switching mats, not on every select
    this->customMaterial->purge();

    this->purge();
//    qDebug() << text;
//    this->meshNode->customMaterial = iris::CustomMaterial::create();
//    this->customMaterial->textureUniforms.clear();
//    this->customMaterial->textureToggleUniforms.clear();
    this->customMaterial->name = text;
//    materialSelector->setCurrentItem(text);

//    if (text == "Default Shader") {
        // collect the height in the widget boi
        this->clearPanel(this->layout());

        resetHeight();

        int finalHeight = 30 + (8 * 28) /* narrows */ + (4 * 108) /* pickers */ + (12 * 6) + 9 + 9;

        setHeight(finalHeight - 30);

        this->setMinimumHeight(finalHeight);
        this->setMaximumHeight(finalHeight);

        this->setSceneNode(this->meshNode);
//    }
//    } else if (text == "Environment Surface Shader") {

//        this->clearPanel(this->layout());

//        resetHeight();

//        // this isn't magic, there are values but it's not possible to get them yet
//        // finalHeight = minimum_height + (widgets * height) +
//        //              (widgetCount * heights) + topMargin + bottomMargin;
//        int finalHeight = 30 + (4 * 28) + (4 * 6) + 9 + 9;

//        setHeight(finalHeight - 30);

//        this->setMinimumHeight(finalHeight);
//        this->setMaximumHeight(finalHeight);

//        this->setSceneNode(this->meshNode);
//    }
}
