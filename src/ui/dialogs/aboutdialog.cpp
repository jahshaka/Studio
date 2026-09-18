/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "ui/dialogs/aboutdialog.h"
#include "ui_aboutdialog.h"
#include "ui/style/stylesheet.h"
#include "ui/dialogs/noticesdialog.h"

#include <QBoxLayout>
#include <QPushButton>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    // aboutdialog.ui used to embed these (classic-only now; theme sweep)
    setStyleSheet(StyleSheet::AboutDialogRoot());
    ui->textBrowser->setStyleSheet(StyleSheet::AboutDialogTextBrowser());
    this->setWindowTitle("About");

    // THIRD-PARTY NOTICES (NOTICES-1, 2026-09-18). The one door: About is where
    // a person looks for what an application is made of, and until now this
    // binary named none of the ten components vendored inside it. Added beside
    // the OK button in code rather than in the .ui, so the classic sheet this
    // dialog carries keeps applying to the button it already styles.
    auto *notices = new QPushButton(tr("Third-party notices"), this);
    notices->setObjectName(QStringLiteral("noticesButton"));
    notices->setCursor(Qt::PointingHandCursor);
    if (auto *row = qobject_cast<QBoxLayout *>(ui->okButton->parentWidget()->layout()))
        row->insertWidget(row->indexOf(ui->okButton), notices);
    connect(notices, &QPushButton::clicked, this, [this] {
        // Parented to THIS dialog, and deleted on close: About can be opened
        // again and must not accumulate pages (the theme.sheets walk opens
        // every dialog twice for exactly this class of defect).
        auto *page = new NoticesDialog(this);
        page->setAttribute(Qt::WA_DeleteOnClose);
        page->open();
    });

    connect(ui->okButton,SIGNAL(clicked(bool)),this,SLOT(close()));
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
