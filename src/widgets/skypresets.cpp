#include "skypresets.h"
#include "ui_skypresets.h"

SkyPresets::SkyPresets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SkyPresets)
{
    ui->setupUi(this);

    ui->skyList->setViewMode(QListWidget::IconMode);
    ui->skyList->setIconSize(QSize(110,80));
    ui->skyList->setResizeMode(QListWidget::Adjust);
    ui->skyList->setMovement(QListView::Static);
    ui->skyList->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->skyList->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/default.png"),"Default"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_002.jpg"),"Sky 1"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_032.jpg"),"Sky 2"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_033.jpg"),"Sky 3"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_002.jpg"),"Sky 1"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_032.jpg"),"Sky 2"));
    ui->skyList->addItem(new QListWidgetItem(QIcon(":/app/content/skies/vp_sky_v2_033.jpg"),"Sky 3"));
}

SkyPresets::~SkyPresets()
{
    delete ui;
}
