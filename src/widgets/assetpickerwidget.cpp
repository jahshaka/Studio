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

    connect(ui->scanBtn,    SIGNAL(pressed()), this, SLOT(refreshList()));

    this->type = type;
    populateWidget();

    this->show();
}

AssetPickerWidget::~AssetPickerWidget()
{
    delete ui;
}

void AssetPickerWidget::populateWidget()
{
    for (auto asset : AssetManager::assets) {
        QPixmap pixmap;

        if (asset->type == type) {
            QFileInfo file(asset->fileName);
            if (file.suffix() == "jpg" || file.suffix() == "png" || file.suffix() == "bmp") {
                auto thumb = ThumbnailManager::createThumbnail(asset->path, 128, 128);
                pixmap = QPixmap::fromImage(*thumb->thumb);
            } else if (file.suffix() == "obj" || file.suffix() == "fbx") {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/user-account-box.svg", 128, 128);
                pixmap = QPixmap::fromImage(*thumb->thumb);
            } else {
                auto thumb = ThumbnailManager::createThumbnail(":/app/icons/google-drive-file.svg", 128, 128);
                pixmap = QPixmap::fromImage(*thumb->thumb);
            }

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

void AssetPickerWidget::refreshList()
{
    ui->assetView->clear();
    populateWidget();
}

bool AssetPickerWidget::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}
