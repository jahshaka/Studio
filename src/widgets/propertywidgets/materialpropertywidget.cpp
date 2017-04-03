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
#include "../checkboxwidget.h"
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

    /// TODO see if this can be removed this when the default material is deleted.
    unsigned sliderSize, textureSize, colorSize, boolSize;
    sliderSize = textureSize = colorSize = boolSize = 0;

    for (int i = 0; i < widgetProps.size(); i++) {
        auto prop = widgetProps[i].toObject();
        if (prop["type"] == "slider")   sliderSize++;
        if (prop["type"] == "color")    colorSize++;
        if (prop["type"] == "checkbox") boolSize++;
        if (prop["type"] == "texture")  textureSize++;
    }

    for (int propIndex = 0; propIndex < widgetProps.size(); propIndex++) {
        auto prop = widgetProps[propIndex].toObject();

        auto displayName = prop["displayName"].toString();
        auto uniform     = prop["uniform"].toString();
        auto name        = prop["name"].toString();

        if (sliderUniforms.size() < sliderSize) {
            if (prop["type"] == "slider") {
                auto start  = (float) prop["start"].toDouble();
                auto end    = (float) prop["end"].toDouble();

                sliderUniforms.push_back(iris::make_mat_struct(sliderUniforms.size() - 1,
                                                               name,
                                                               uniform,
                                                               addFloatValueSlider(displayName,
                                                                                   start, end)));
                sliderUniforms.back().value->index = sliderUniforms.size() - 1;
            }
        }

        if (colorUniforms.size() < colorSize) {
            if (prop["type"] == "color") {
                colorUniforms.push_back(
                            iris::make_mat_struct(colorUniforms.size() - 1,
                                                  name,
                                                  uniform,
                                                  addColorPicker(displayName)));
                colorUniforms.back().value->index = colorUniforms.size() - 1;
                colorUniforms.back().value->getPicker()->index = colorUniforms.size() - 1;
            }
        }

        if (boolUniforms.size() < boolSize) {
            if (prop["type"] == "checkbox") {
                auto value = prop["value"].toBool();
                boolUniforms.push_back(
                            iris::make_mat_struct(boolUniforms.size() - 1,
                                                  name,
                                                  uniform,
                                                  addCheckBox(displayName, value)));
                boolUniforms.back().value->index = boolUniforms.size() - 1;
            }
        }

        if (textureUniforms.size() < textureSize) {
            if (prop["type"] == "texture") {
                textureUniforms.push_back(
                            iris::make_mat_struct(textureUniforms.size() - 1,
                                                  name,
                                                  uniform,
                                                  addTexturePicker(displayName)));
                textureUniforms.back().value->index = textureUniforms.size() - 1;
            }
        }
    }
}

void MaterialPropertyWidget::setupCustomMaterial()
{
    createWidgets(this->customMaterial->getShaderFile());

    // sliders
    QSignalMapper *sliderMapper = new QSignalMapper(this);
    auto sliderIter = sliderUniforms.begin();
    while (sliderIter != sliderUniforms.end()) {
        connect(sliderIter->value, SIGNAL(valueChanged(float)), sliderMapper, SLOT(map()));
        sliderMapper->setMapping(sliderIter->value, sliderIter->value);
        ++sliderIter;
    }
    connect(sliderMapper, SIGNAL(mapped(QWidget*)), SLOT(onCustomSliderChanged(QWidget*)));

    // colors
    QSignalMapper *colorMapper = new QSignalMapper(this);
    auto colorIter = colorUniforms.begin();
    while (colorIter != colorUniforms.end()) {
        connect(colorIter->value->getPicker(),  SIGNAL(onColorChanged(QColor)),
                colorMapper,                    SLOT(map()));
        colorMapper->setMapping(colorIter->value->getPicker(), colorIter->value->getPicker());
        ++colorIter;
    }
    connect(colorMapper, SIGNAL(mapped(QWidget*)), SLOT(onCustomColorChanged(QWidget*)));

    // bools
    QSignalMapper *boolMapper = new QSignalMapper(this);
    auto boolIter = boolUniforms.begin();
    while (boolIter != boolUniforms.end()) {
        connect(boolIter->value, SIGNAL(valueChanged(bool)), boolMapper, SLOT(map()));
        boolMapper->setMapping(boolIter->value, boolIter->value);
        ++boolIter;
    }
    connect(boolMapper, SIGNAL(mapped(QWidget*)), SLOT(onCheckBoxStateChanged(QWidget*)));

    // textures
    QSignalMapper *textureMapper = new QSignalMapper(this);
    auto texIter = textureUniforms.begin();
    while (texIter != textureUniforms.end()) {
        connect(texIter->value, SIGNAL(valueChanged(QString)), textureMapper, SLOT(map()));
        textureMapper->setMapping(texIter->value, texIter->value);
        ++texIter;
    }
    connect(textureMapper, SIGNAL(mapped(QWidget*)), SLOT(onCustomTextureChanged(QWidget*)));

    // iterate both (different typed) maps in lockstep...
    // we get the value from the material and set the widget value

    // SLIDERS
    sliderIter = sliderUniforms.begin();
    auto sIter = customMaterial->sliderUniforms.begin();
    while (sliderIter != sliderUniforms.end()) {
        sliderIter->value->setValue(sIter->value);
        ++sIter;
        ++sliderIter;
    }

    // COLORS
    colorIter = colorUniforms.begin();
    auto cIter = customMaterial->colorUniforms.begin();
    while (colorIter != colorUniforms.end()) {
        colorIter->value->setColorValue(cIter->value);
        ++colorIter;
        ++cIter;
    }

    // BOOLS
    boolIter = boolUniforms.begin();
    auto bIter = customMaterial->boolUniforms.begin();
    while (boolIter != boolUniforms.end()) {
        boolIter->value->setValue(bIter->value);
        ++boolIter;
        ++bIter;
    }

    // TExTURES
    texIter = textureUniforms.begin();
    auto tIter = customMaterial->textureUniforms.begin();
    while (texIter != textureUniforms.end()) {
        if (!tIter->value.isEmpty()) {
            texIter->value->setTexture(tIter->value);
        } else {
            texIter->value->setTexture("");
        }
        ++texIter;
        ++tIter;
    }
}

void MaterialPropertyWidget::forceShaderRefresh(const QString &matName)
{
    emit onMaterialSelectorChanged(matName);
}

void MaterialPropertyWidget::setupShaderSelector()
{
    materialSelector = this->addComboBox("Shader");

    QDir dir(IrisUtils::getAbsoluteAssetPath("app/shader_defs/"));
    for (auto shaderName : dir.entryList(QDir::Files)) {
        materialSelector->addItem(QFileInfo(shaderName).baseName());
    }

    materialSelector->setCurrentItem(this->customMaterial->getMaterialName());

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
        materialReader->readJahShader(
                    IrisUtils::getAbsoluteAssetPath(
                        "app/shader_defs/" + this->customMaterial->getMaterialName() + ".json"));
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

void MaterialPropertyWidget::onCheckBoxStateChanged(QWidget *t)
{
    auto changedIndex = dynamic_cast<CheckBoxWidget*>(t);
    if (!!customMaterial) {
        customMaterial->boolUniforms[changedIndex->index].value = changedIndex->getValue();
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
    boolUniforms.clear();
    textureUniforms.clear();
}

void MaterialPropertyWidget::onMaterialSelectorChanged(const QString &text)
{
    // only clear when we are switching mats, not on every select
    this->customMaterial->purge();
    this->purge();

    this->clearPanel(this->layout());

    this->customMaterial->setMaterialName(text);
    this->setSceneNode(this->meshNode);
    int finalHeight = this->customMaterial->getCalculatedPropHeight();

    resetHeight();
    setHeight(finalHeight);

    this->setMinimumHeight(finalHeight);
    this->setMaximumHeight(finalHeight);
}
