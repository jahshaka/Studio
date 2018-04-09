#include "softwareupdatedialog.h"
#include "ui_softwareupdatedialog.h"

#include <QDesktopServices>
#include <QDebug>
#include "core/settingsmanager.h"


SoftwareUpdateDialog::SoftwareUpdateDialog(QDialog *parent) : QDialog(parent), ui(new Ui::SoftwareUpdateDialog)
{
	ui->setupUi(this);
	setWindowTitle("Software Update");
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	connect(ui->pushButton, &QPushButton::clicked, [this]() {
		qDebug() << downloadUrl;
		QDesktopServices::openUrl(downloadUrl);
	});

	connect(ui->pushButton_2, &QPushButton::clicked, [this]() {
		this->close();
	});

	auto updates = SettingsManager::getDefaultManager()->getValue("automatic_updates", true).toBool();
	ui->checkBox->setChecked(updates);

	connect(ui->checkBox, &QCheckBox::clicked, [this](bool checked) {
		SettingsManager::getDefaultManager()->setValue("automatic_updates", checked);
	});
}

void SoftwareUpdateDialog::setVersionNotes(QString notes)
{
	ui->textEdit->setHtml(notes);
}

void SoftwareUpdateDialog::setDownloadUrl(QString url)
{
	this->downloadUrl = url;
}

SoftwareUpdateDialog::~SoftwareUpdateDialog()
{
	delete ui;
}