#include "donatedialog.h"
#include "ui_donate.h"
#include "../core/settingsmanager.h"

DonateDialog::DonateDialog(QDialog *parent) : QDialog(parent), ui(new Ui::DonateDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::FramelessWindowHint);

    connect(ui->checkBox, SIGNAL(pressed()), this, SLOT(saveAndClose()));
    connect(ui->close, SIGNAL(pressed()), this, SLOT(close()));

    settingsManager = SettingsManager::getDefaultManager();
}

DonateDialog::~DonateDialog()
{
    delete ui;
}

void DonateDialog::saveAndClose()
{
    settingsManager->setValue("ddialog_seen", true);
}

void DonateDialog::changeEvent(QEvent * event)
{
	if (event->type() == QEvent::LanguageChange)	ui->retranslateUi(this);
	QWidget::changeEvent(event);
}