#include "renameprojectdialog.h"
#include "ui_renameprojectdialog.h"

RenameProjectDialog::RenameProjectDialog(QDialog *parent) : QDialog(parent), ui(new Ui::RenameProjectDialog)
{
    ui->setupUi(this);
    setWindowTitle("Rename Project");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	ui->ok->setAutoDefault(true);
	ui->ok->setDefault(true);

    ui->lineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->ok, SIGNAL(pressed()), SLOT(newText()));
    connect(ui->cancel, &QPushButton::pressed, [this]() { close(); });
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
