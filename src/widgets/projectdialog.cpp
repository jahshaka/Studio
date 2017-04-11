#include <QFile>
#include <QFontDatabase>

#include "src/irisgl/src/core/irisutils.h"
#include "../mainwindow.h"
#include "../core/settingsmanager.h"

#include "projectdialog.h"
#include "ui_projectdialog.h"

#include <QDebug>

ProjectDialog::ProjectDialog(QDialog *parent) : QDialog(parent), ui(new Ui::ProjectDialog)
{
    ui->setupUi(this);

    this->setWindowTitle("Jahshaka VR");

    QFile fontFile(IrisUtils::getAbsoluteAssetPath("app/fonts/OpenSans-Bold.ttf"));
    if (fontFile.exists()) {
        fontFile.open(QIODevice::ReadOnly);
        QFontDatabase::addApplicationFontFromData(fontFile.readAll());
        QApplication::setFont(QFont("Open Sans", 9));
    }

    connect(ui->newProject, SIGNAL(pressed()), SLOT(newScene()));

    window = new MainWindow;

    auto sm = window->getSettingsManager();
//    qDebug() << sm->getRecentlyOpenedScenes();

    ui->listWidget->addItems(sm->getRecentlyOpenedScenes());
}

ProjectDialog::~ProjectDialog()
{
    delete ui;
}

void ProjectDialog::newScene()
{
    window->showMaximized();
    window->newScene();

    this->close();
    emit this->accepted();
}
