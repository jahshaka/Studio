#include "getnamedialog.h"
#include "ui_getnamedialog.h"

GetNameDialog::GetNameDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GetNameDialog)
{
    ui->setupUi(this);
}

GetNameDialog::~GetNameDialog()
{
    delete ui;
}

void GetNameDialog::setName(QString name)
{
    ui->lineEdit->setText(name);
    ui->lineEdit->selectAll();
}

QString GetNameDialog::getName()
{
    return ui->lineEdit->text();
}

void GetNameDialog::changeEvent(QEvent * event)
{
	if (event->type() == QEvent::LanguageChange)	ui->retranslateUi(this);
	QWidget::changeEvent(event);
}
