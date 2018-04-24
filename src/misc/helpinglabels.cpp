#include "helpinglabels.h"
#include "ui_helpinglabels.h"

HelpingLabels::HelpingLabels(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::HelpingLabels)
{
    ui->setupUi(this);
}

HelpingLabels::~HelpingLabels()
{
    delete ui;
}

void HelpingLabels::changeEvent(QEvent * event)
{

	if (event->type() == QEvent::LanguageChange)
	{
	
		ui->retranslateUi(this);
	}
	QWidget::changeEvent(event);

}
