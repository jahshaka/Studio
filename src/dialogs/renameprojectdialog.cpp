#include "renameprojectdialog.h"
#include "ui_renameprojectdialog.h"

RenameProjectDialog::RenameProjectDialog(QDialog *parent) : QDialog(parent), ui(new Ui::RenameProjectDialog)
{
    ui->setupUi(this);
    setWindowTitle("Rename Project");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	ui->ok->setAutoDefault(true);
	ui->ok->setDefault(true);

    connect(ui->ok, SIGNAL(pressed()), SLOT(newText()));
}

RenameProjectDialog::~RenameProjectDialog()
{
    delete ui;
}

void RenameProjectDialog::newText()
{
    emit newTextEmit(ui->lineEdit->text());
    this->close();
}
