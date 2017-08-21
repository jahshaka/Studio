#include "assetpickerwidget.h"
#include "ui_assetpickerwidget.h"
#include "../core/thumbnailmanager.h"

AssetPickerWidget::AssetPickerWidget(AssetType type, QDialog *parent) :
    QDialog(parent),
    ui(new Ui::AssetPickerWidget)
{
    ui->setupUi(this);

    setWindowTitle("Select Asset");
    ui->viewButton->setCheckable(true);
    ui->viewButton->setToolTip("Toggle icon view");

    connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,           SLOT(assetViewDblClicked(QListWidgetItem*)));

    connect(ui->scanBtn,    SIGNAL(pressed()), this, SLOT(refreshList()));
    connect(ui->viewButton, SIGNAL(toggled(bool)), this, SLOT(changeView(bool)));

    connect(ui->searchBar,  SIGNAL(textChanged(QString)),
            this,           SLOT(searchAssets(QString)));

    this->type = type;
    populateWidget();

    ui->assetView->setViewMode(QListWidget::ListMode);
    ui->assetView->setIconSize(QSize(32, 32));
    ui->assetView->setSpacing(4);

    this->show();
}

AssetPickerWidget::~AssetPickerWidget()
{
    delete ui;
}

void AssetPickerWidget::populateWidget(QString filter)
{
    for (auto asset : AssetManager::assets) {
        QPixmap pixmap;

        if (asset->type == type) {
            QFileInfo file(asset->fileName);
            auto item = new QListWidgetItem(asset->fileName);

            if (file.suffix() == "jpg" || file.suffix() == "png" || file.suffix() == "bmp") {
                auto thumb = ThumbnailManager::createThumbnail(asset->path, 128, 128);
                pixmap = QPixmap::fromImage(*thumb->thumb);
                item->setIcon(QIcon(pixmap));
            } else if (file.suffix() == "obj" || file.suffix() == "fbx") {
                item->setIcon(QIcon(":/icons/user-account-box.svg"));
            } else {
                item->setIcon(QIcon(":/icons/google-drive-file.svg"));
            }

            item->setData(Qt::UserRole, asset->path);

            if (filter.isEmpty()) {
                ui->assetView->addItem(item);
            } else {
                if (asset->fileName.contains(filter)) {
                    ui->assetView->addItem(item);
                }
            }
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

void AssetPickerWidget::changeView(bool toggle)
{
    if (toggle) {
        ui->assetView->setViewMode(QListWidget::IconMode);
        ui->assetView->setIconSize(QSize(88, 88));
        ui->assetView->setResizeMode(QListWidget::Adjust);
//        ui->assetView->setMovement(QListView::Static);
//        ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
        ui->assetView->setSelectionMode(QAbstractItemView::SingleSelection);

        for (int i = 0; i < ui->assetView->count(); ++i) {
            auto item = ui->assetView->item(i);
            item->setSizeHint(QSize(128, 128));
        }
    } else {
        ui->assetView->setViewMode(QListWidget::ListMode);
        ui->assetView->setIconSize(QSize(32, 32));
        ui->assetView->setSpacing(4);

        for (int i = 0; i < ui->assetView->count(); ++i) {
            auto item = ui->assetView->item(i);
            item->setSizeHint(QSize(32, 32));
        }
    }
}

void AssetPickerWidget::searchAssets(QString searchString)
{
    ui->assetView->clear();
    populateWidget(searchString);
}

bool AssetPickerWidget::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}
