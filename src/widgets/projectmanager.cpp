#include "projectmanager.h"
#include "ui_projectmanager.h"

ProjectManager::ProjectManager(QWidget *parent) : QWidget(parent), ui(new Ui::ProjectManager)
{
    ui->setupUi(this);
}

ProjectManager::~ProjectManager()
{
    delete ui;
}
