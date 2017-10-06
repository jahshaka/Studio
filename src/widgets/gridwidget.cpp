#include "gridwidget.h"

#include "../core/settingsmanager.h"
#include "../constants.h"

GridWidget::GridWidget(ProjectTileData tileData, QWidget *parent) : QWidget(parent)
{
    auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = SettingsManager::getDefaultManager()->getValue("default_directory", spath).toString();

    setFocusPolicy(Qt::StrongFocus);

    this->path = QDir(projectFolder).filePath(tileData.name);

    if (!tileData.thumbnail.isEmpty() || !tileData.thumbnail.isNull()) {
        QPixmap cachedPixmap;
        if (cachedPixmap.loadFromData(tileData.thumbnail, "PNG")) image = cachedPixmap;
    } else {
        image = QPixmap::fromImage(QImage(":/images/preview.png"));
    }

    projectName = this->path + "/" + tileData.name + ".jah";
}
