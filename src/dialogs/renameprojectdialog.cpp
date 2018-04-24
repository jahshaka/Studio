#include "renameprojectdialog.h"
#include "ui_renameprojectdialog.h"

RenameProjectDialog::RenameProjectDialog(QDialog *parent) : QDialog(parent), ui(new Ui::RenameProjectDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("Rename Project"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

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

void RenameProjectDialog::changeEvent(QEvent * event)
{
	if (event->type() == QEvent::LanguageChange)	ui->retranslateUi(this);
	QWidget::changeEvent(event);
}
