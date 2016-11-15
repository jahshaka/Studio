#include "transformeditor.h"
#include "ui_transformeditor.h"

#include "../../jah3d/core/scenenode.h"

TransformEditor::TransformEditor(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TransformEditor)
{
    ui->setupUi(this);

    //translation
    connect(ui->xpos,SIGNAL(valueChanged(double)),this,SLOT(xPosChanged(float)));
    connect(ui->ypos,SIGNAL(valueChanged(double)),this,SLOT(yPosChanged(float)));
    connect(ui->zpos,SIGNAL(valueChanged(double)),this,SLOT(zPosChanged(float)));

    //rotation
    connect(ui->xrot,SIGNAL(valueChanged(double)),this,SLOT(xRotChanged(float)));
    connect(ui->yrot,SIGNAL(valueChanged(double)),this,SLOT(yRotChanged(float)));
    connect(ui->zrot,SIGNAL(valueChanged(double)),this,SLOT(zRotChanged(float)));

    //scale
    connect(ui->xscale,SIGNAL(valueChanged(double)),this,SLOT(xScaleChanged(float)));
    connect(ui->yscale,SIGNAL(valueChanged(double)),this,SLOT(yScaleChanged(float)));
    connect(ui->zscale,SIGNAL(valueChanged(double)),this,SLOT(zScaleChanged(float)));

}

TransformEditor::~TransformEditor()
{
    delete ui;
}

void TransformEditor::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    this->sceneNode = sceneNode;
}

void TransformEditor::xPosChanged(float value)
{
    if(!!sceneNode)
    {
        sceneNode->pos.setX(value);
    }
}

void TransformEditor::yPosChanged(float value)
{
    if(!!sceneNode)
    {
        sceneNode->pos.setY(value);
    }
}

void TransformEditor::zPosChanged(float value)
{
    if(!!sceneNode)
    {
        sceneNode->pos.setZ(value);
    }
}

/**
 * rotation change callbacks
 */
void TransformEditor::xRotChanged(float value)
{
    if(!!sceneNode)
    {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setX(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

void TransformEditor::yRotChanged(float value)
{
    if(!!sceneNode)
    {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setY(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

void TransformEditor::zRotChanged(float value)
{
    if(!!sceneNode)
    {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setZ(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

/**
 * scale change callbacks
 */
void TransformEditor::xScaleChanged(float value)
{
    if(!!sceneNode)
    {
        sceneNode->scale.setX(value);
    }
}

void TransformEditor::yScaleChanged(float value)
{
    if(!!sceneNode)
    {
        sceneNode->scale.setY(value);
    }
}

void TransformEditor::zScaleChanged(float value)
{
    if(!!sceneNode)
    {
        sceneNode->scale.setZ(value);
    }
}

