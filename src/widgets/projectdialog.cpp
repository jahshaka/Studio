#include <QFile>
#include <QFontDatabase>

#include "src/irisgl/src/core/irisutils.h"
#include "../mainwindow.h"
#include "../core/settingsmanager.h"

#include "../dialogs/newprojectdialog.h"

#include "projectdialog.h"
#include "ui_projectdialog.h"

#include "../core/project.h"
#include "../globals.h"

#include <QDebug>
#include <QFileDialog>

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

    connect(ui->newProject,     SIGNAL(pressed()), SLOT(newScene()));
    connect(ui->openProject,    SIGNAL(pressed()), SLOT(openProject()));

//    window = new MainWindow;

//    auto sm = window->getSettingsManager();
//    ui->listWidget->addItems(sm->getRecentlyOpenedScenes());
}

ProjectDialog::~ProjectDialog()
{
    delete ui;
}

void ProjectDialog::newScene()
{
//    NewProjectDialog dialog;

//    dialog.exec();

//    auto projectName = dialog.getProjectInfo().projectName;
//    auto projectPath = dialog.getProjectInfo().projectPath;

//    if (!projectName.isEmpty() || !projectName.isNull()) {
//        window->showMaximized();
//        window->newProject(projectName, projectPath);

//        this->close();
//        // emit accepted();
//    }
}

void ProjectDialog::openProject()
{
    auto project = loadSceneDelegate();

    // hacky as fuck, fix please TODO
    // use a FOLDER approach like Unity? can we detect if folder is a project like it does??
    QFileInfo finfo = QFileInfo(project);
    QDir dirr = finfo.absoluteDir();
    dirr.cdUp();
    Globals::project->updateProjectPath(dirr.absolutePath());

    window = new MainWindow;
    window->showMaximized();
    window->openProject(project);

    this->close();
}

QString ProjectDialog::loadSceneDelegate()
{
    QString dir = QApplication::applicationDirPath() + "/scenes/";
    auto filename = QFileDialog::getOpenFileName(this, "Open Scene File", dir, "Jashaka Scene (*.jah)");

    if (filename.isEmpty() || filename.isNull()) return "";

    return filename;
}

