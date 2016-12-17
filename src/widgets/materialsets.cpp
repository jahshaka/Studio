#include "materialsets.h"
#include "ui_materialsets.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QDebug>

#include "../io/materialpresetreader.h"
#include "../core/materialpreset.h"

#include "../mainwindow.h"

MaterialSets::MaterialSets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MaterialSets)
{
    ui->setupUi(this);

    mainWindow = nullptr;

    ui->materialPresets->setViewMode(QListWidget::IconMode);
    ui->materialPresets->setIconSize(QSize(65,65));
    ui->materialPresets->setResizeMode(QListWidget::Adjust);
    ui->materialPresets->setMovement(QListView::Static);
    ui->materialPresets->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->materialPresets->setSelectionMode(QAbstractItemView::SingleSelection);

    /*
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
    */

    connect(ui->materialPresets,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(applyMaterialPreset(QListWidgetItem*)));

    loadPresets();

}

void MaterialSets::loadPresets()
{
    auto dir = QDir("app/content/materials");
    auto files = dir.entryInfoList(QStringList(),QDir::Files);

    auto reader = new MaterialPresetReader();

    for(auto file:files)
    {
        auto preset = reader->readMaterialPreset(file.absoluteFilePath());
        addPreset(preset);
    }
}

void MaterialSets::addPreset(MaterialPreset* preset)
{
    presets.append(preset);

    auto item = new QListWidgetItem(QIcon(preset->icon),preset->name);
    item->setData(Qt::UserRole,presets.count()-1);
    ui->materialPresets->addItem(item);
}

void MaterialSets::applyMaterialPreset(QListWidgetItem* item)
{
    if(!mainWindow)
        return;

    auto preset = presets[item->data(Qt::UserRole).toInt()];
    mainWindow->applyMaterialPreset(preset);
}

MaterialSets::~MaterialSets()
{
    delete ui;

    for(auto preset:presets)
        delete preset;
}
