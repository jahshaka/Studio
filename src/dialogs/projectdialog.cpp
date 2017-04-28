#include <QFile>
#include <QFontDatabase>

#include "src/irisgl/src/core/irisutils.h"
#include "../mainwindow.h"
#include "../core/settingsmanager.h"

#include "newprojectdialog.h"

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

//    ui->listWidget->viewport()->installEventFilter(this);

    connect(ui->newProject,     SIGNAL(pressed()), SLOT(newScene()));
    connect(ui->openProject,    SIGNAL(pressed()), SLOT(openProject()));
    connect(ui->listWidget,     SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openRecentProject(QListWidgetItem*)));

//    window = new MainWindow;
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
        auto pPath = projectPath + '/' + projectName;
        auto str = pPath + "/Scenes/" + projectName + ".jah";

        Globals::project->setFilePath(str);
        Globals::project->updateProjectPath(pPath);

        // make a dir and the subfolders...
        QDir dir(pPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        // make proj folders
        QList<QString> projFolders = { "Textures", "Models", "Shaders", "Materials", "Scenes" };
        for (auto folder : projFolders) {
            QDir dir(pPath + '/' + folder);
            dir.mkpath(".");
        }

        window = new MainWindow;
        window->showMaximized();
        window->newProject(projectName, projectPath);

        this->close();
        // emit accepted();
    }
}

void ProjectDialog::openProject()
{
    auto project = loadSceneDelegate();

    if (!project.isEmpty()) {
        // TODO use a FOLDER approach like Unity? can we detect if folder is a project like it does??
        QFileInfo finfo = QFileInfo(project);
        QDir dirr = finfo.absoluteDir();
        dirr.cdUp();
        Globals::project->updateProjectPath(dirr.absolutePath());

        window = new MainWindow;
        window->showMaximized();
        window->openProject(project);

        this->close();
    }
}

QString ProjectDialog::loadSceneDelegate()
{
    QString dir = QApplication::applicationDirPath() + "/scenes/";
    auto filename = QFileDialog::getOpenFileName(this, "Open Scene File", dir, "Jashaka Scene (*.jah)");
    return filename;
}

void ProjectDialog::openRecentProject(QListWidgetItem *item)
{
    // TODO use a FOLDER approach like Unity? can we detect if folder is a project like it does??
    QFileInfo finfo = QFileInfo(item->text());
    QDir dirr = finfo.absoluteDir();
    dirr.cdUp();
    Globals::project->updateProjectPath(dirr.absolutePath());

    window = new MainWindow;
    window->showMaximized();
    window->openProject(item->text());
}

SettingsManager *ProjectDialog::getSettingsManager()
{
    return settings;
}

bool ProjectDialog::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}

