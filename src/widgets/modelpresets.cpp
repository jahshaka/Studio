#include "modelpresets.h"
#include "ui_modelpresets.h"

ModelPresets::ModelPresets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ModelPresets)
{
    ui->setupUi(this);

    ui->modelsSets->setViewMode(QListWidget::IconMode);
    ui->modelsSets->setIconSize(QSize(88,88));
    ui->modelsSets->setResizeMode(QListWidget::Adjust);
    ui->modelsSets->setMovement(QListView::Static);
    ui->modelsSets->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->modelsSets->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/cone.png"),"Cone"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/cube.png"),"Cube"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/cylinder.png"),"Cylinder"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/icosphere.png"),"IcoSphere"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/sphere.png"),"Sphere"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/torus.png"),"Torus"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/cone.png"),"Cone"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/cube.png"),"Cube"));
    ui->modelsSets->addItem(new QListWidgetItem(QIcon(":/app/modelpresets/cylinder.png"),"Cylinder"));

}

ModelPresets::~ModelPresets()
{
    delete ui;
}
