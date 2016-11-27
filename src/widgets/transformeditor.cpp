#include "transformeditor.h"
#include "ui_transformeditor.h"

#include "../../jah3d/core/scenenode.h"

TransformEditor::TransformEditor(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TransformEditor)
{
    ui->setupUi(this);

    //translation
    connect(ui->xpos,SIGNAL(valueChanged(double)),this,SLOT(xPosChanged(double)));
    connect(ui->ypos,SIGNAL(valueChanged(double)),this,SLOT(yPosChanged(double)));
    connect(ui->zpos,SIGNAL(valueChanged(double)),this,SLOT(zPosChanged(double)));

    //rotation
    connect(ui->xrot,SIGNAL(valueChanged(double)),this,SLOT(xRotChanged(double)));
    connect(ui->yrot,SIGNAL(valueChanged(double)),this,SLOT(yRotChanged(double)));
    connect(ui->zrot,SIGNAL(valueChanged(double)),this,SLOT(zRotChanged(double)));

    //scale
    connect(ui->xscale,SIGNAL(valueChanged(double)),this,SLOT(xScaleChanged(double)));
    connect(ui->yscale,SIGNAL(valueChanged(double)),this,SLOT(yScaleChanged(double)));
    connect(ui->zscale,SIGNAL(valueChanged(double)),this,SLOT(zScaleChanged(double)));

}

TransformEditor::~TransformEditor()
{
    delete ui;
}

void TransformEditor::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    this->sceneNode = sceneNode;

    if(!!sceneNode)
    {
        ui->xpos->setValue(sceneNode->pos.x());
        ui->ypos->setValue(sceneNode->pos.y());
        ui->zpos->setValue(sceneNode->pos.z());

        auto rot = sceneNode->rot.toEulerAngles();
        ui->xrot->setValue(rot.x());
        ui->yrot->setValue(rot.y());
        ui->zrot->setValue(rot.z());

        ui->xscale->setValue(sceneNode->scale.x());
        ui->yscale->setValue(sceneNode->scale.y());
        ui->zscale->setValue(sceneNode->scale.z());
    }
}

void TransformEditor::xPosChanged(double value)
{
    if(!!sceneNode)
    {
        sceneNode->pos.setX(value);
    }
}

void TransformEditor::yPosChanged(double value)
{
    if(!!sceneNode)
    {
        sceneNode->pos.setY(value);
    }
}

void TransformEditor::zPosChanged(double value)
{
    if(!!sceneNode)
    {
        sceneNode->pos.setZ(value);
    }
}

/**
 * rotation change callbacks
 */
void TransformEditor::xRotChanged(double value)
{
    if(!!sceneNode)
    {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setX(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

void TransformEditor::yRotChanged(double value)
{
    if(!!sceneNode)
    {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setY(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

void TransformEditor::zRotChanged(double value)
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
void TransformEditor::xScaleChanged(double value)
{
    if(!!sceneNode)
    {
        sceneNode->scale.setX(value);
    }
}

void TransformEditor::yScaleChanged(double value)
{
    if(!!sceneNode)
    {
        sceneNode->scale.setY(value);
    }
}

void TransformEditor::zScaleChanged(double value)
{
    if(!!sceneNode)
    {
        sceneNode->scale.setZ(value);
    }
}

