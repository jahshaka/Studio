/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>

namespace Ui {
    class ProgressDialog;
}

class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    ProgressDialog(QDialog *parent = nullptr);
    ~ProgressDialog();

    /// True after the user pressed Cancel (or Esc) — the accessor the import
    /// pipeline's ImportProgressFn contract was missing. Sticky until
    /// resetCancel().
    bool wasCanceled() const { return mCanceled; }

public slots:
    void setLabelText(const QString&);
    /// The secondary, dimmer line under the title ("Extracting textures (3/12)…").
    void setStageText(const QString&);
    void setRange(int, int);
    void setValue(int);
	void setValueAndText(int value, QString text);
    void reset();
    /// Clear the cancel latch and restore the Cancel button for a new run.
    void resetCancel();
    /// Show/hide the Cancel button (rows that cannot cancel hide it).
    void setCancelVisible(bool visible);

signals:
    /// The user asked to cancel (button or Esc). Emitted once per latch.
    void canceled();

protected:
    /// Esc lands here on a QDialog — treat it as Cancel, never a silent close.
    void reject() override;

private:
    Ui::ProgressDialog *ui;
    bool mCanceled = false;
};

#endif // PROGRESSDIALOG_H
