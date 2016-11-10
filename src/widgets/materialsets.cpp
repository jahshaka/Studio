#include "materialsets.h"
#include "ui_materialsets.h"

MaterialSets::MaterialSets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MaterialSets)
{
    ui->setupUi(this);

    ui->materialPresets->setViewMode(QListWidget::IconMode);
    ui->materialPresets->setIconSize(QSize(65,65));
    ui->materialPresets->setResizeMode(QListWidget::Adjust);
    ui->materialPresets->setMovement(QListView::Static);
    ui->materialPresets->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->materialPresets->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/abstractpattern.png"),"Earth"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/agedbrickwall.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/ancientfloorbrick.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/bamboo.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/barkwood.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/brickwall.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/chboard.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/floralpattern.png"),"Tornado"));
    //ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/cball.png"),"Tornado"));
    //ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/grass.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/greenpattern.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/groundboard.png"),"Tornado"));
    ui->materialPresets->addItem(new QListWidgetItem(QIcon(":/app/materials/info_graphic.png"),"Tornado"));



}

MaterialSets::~MaterialSets()
{
    delete ui;
}
