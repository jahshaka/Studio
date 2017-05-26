#include <QFile>
#include <QFontDatabase>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>

#include "src/irisgl/src/core/irisutils.h"
#include "../mainwindow.h"
#include "../core/settingsmanager.h"

#include "newprojectdialog.h"

#include "projectdialog.h"
#include "ui_projectdialog.h"

#include "../core/project.h"
#include "../core/database/database.h"
#include "../globals.h"
#include "../constants.h"

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
    connect(ui->listWidget,     SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openRecentProject(QListWidgetItem*)));

    settings = SettingsManager::getDefaultManager();
    ui->listWidget->addItems(settings->getRecentlyOpenedScenes());
}

ProjectDialog::~ProjectDialog()
{
    delete ui;
}

void ProjectDialog::newScene()
{
    NewProjectDialog dialog;

    dialog.exec();

    auto projectName = dialog.getProjectInfo().projectName;
    auto projectPath = dialog.getProjectInfo().projectPath;

    if (!projectName.isEmpty() || !projectName.isNull()) {
        auto fullProjectPath = QDir(projectPath).filePath(projectName);
        auto slnName = QDir(fullProjectPath).filePath(projectName + Constants::PROJ_EXT);

        Globals::project->setFilePath(slnName);
        Globals::project->setProjectPath(fullProjectPath);

        // make a dir and the default subfolders
        QDir projectDir(fullProjectPath);
        if (!projectDir.exists()) projectDir.mkpath(".");

        for (auto folder : Constants::PROJECT_DIRS) {
            QDir dir(QDir(fullProjectPath).filePath(folder));
            dir.mkpath(".");
        }

        window = new MainWindow;
        window->showMaximized();
        window->newProject(projectName, fullProjectPath);
        settings->addRecentlyOpenedScene(slnName);

        this->close();
    }
}

void ProjectDialog::openProject()
{
    auto projectFile = QFileInfo(loadProjectDelegate());
    auto projectPath = projectFile.absolutePath();

    if (!projectPath.isEmpty()) {
        Globals::project->setProjectPath(projectPath);

        window = new MainWindow;
        window->showMaximized();
        window->openProject(projectFile.absoluteFilePath());

        settings->addRecentlyOpenedScene(projectFile.absoluteFilePath());
        this->close();
    }

    else {
        // QMessageBox msgBox;
        // msgBox.setStyleSheet("QLabel { width: 128px; }");
        // msgBox.setIcon(QMessageBox::Warning);
        // msgBox.setText("Unable to locate project!");
        // msgBox.setInformativeText("You did not select a folder or the path is invalid");
        // msgBox.setStandardButtons(QMessageBox::Ok);
        // msgBox.exec();
    }
}

QString ProjectDialog::loadProjectDelegate()
{
    auto projectFileName = QFileDialog::getOpenFileName(this, "Select Project Folder",
                                                        nullptr, "Jahshaka Project File (*.project)");
    return projectFileName;
}

void ProjectDialog::openRecentProject(QListWidgetItem *item)
{
    Globals::project->setProjectPath(item->text());

    window = new MainWindow;
    window->showMaximized();
    window->openProject(item->text());

    this->close();
}

SettingsManager *ProjectDialog::getSettingsManager()
{
    return settings;
}

bool ProjectDialog::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}

