#include "assetpickerwidget.h"
#include "ui_assetpickerwidget.h"
#include "../core/thumbnailmanager.h"

AssetPickerWidget::AssetPickerWidget(AssetType type, QDialog *parent) :
    QDialog(parent),
    ui(new Ui::AssetPickerWidget)
{
    ui->setupUi(this);

    connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,           SLOT(assetViewDblClicked(QListWidgetItem*)));

    populateWidget(type);

    this->show();
}

AssetPickerWidget::~AssetPickerWidget()
{
    delete ui;
}

void AssetPickerWidget::populateWidget(AssetType type)
{
    for (auto asset : AssetManager::assets) {
        if (asset->type == type) {
            auto t = ThumbnailManager::createThumbnail(asset->path, 128, 128);
            QPixmap pixmap;
            pixmap = pixmap.fromImage(*t->thumb);
            auto item = new QListWidgetItem(QIcon(pixmap), asset->fileName);
            item->setData(Qt::UserRole, asset->path);
            ui->assetView->addItem(item);
        }
    }
}

void AssetPickerWidget::assetViewDblClicked(QListWidgetItem *item)
{
    emit itemDoubleClicked(item);
    this->close();
}

bool AssetPickerWidget::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}
