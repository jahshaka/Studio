#include "progressdialog.h"
#include "ui_progressdialog.h"

#include <QApplication>

ProgressDialog::ProgressDialog(QDialog *parent) : QDialog(parent), ui(new Ui::ProgressDialog)
{
    ui->setupUi(this);
    this->setWindowFlags(Qt::FramelessWindowHint);
	setWindowModality(Qt::WindowModal);
}

ProgressDialog::~ProgressDialog()
{
    delete ui;
}

void ProgressDialog::setLabelText(const QString &text)
{
    ui->label->setText(text);
    QApplication::processEvents();
}

void ProgressDialog::reset()
{
    ui->progressBar->reset();
}

void ProgressDialog::setRange(int min, int max)
{
    ui->progressBar->setRange(min, max);
}

void ProgressDialog::setValue(int val)
{
    ui->progressBar->setValue(val);
	QApplication::processEvents();
}
