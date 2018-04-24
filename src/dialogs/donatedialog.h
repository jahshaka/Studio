#ifndef DONATEDIALOG_HPP
#define DONATEDIALOG_HPP

#include <QDialog>

namespace Ui {
    class DonateDialog;
}

class SettingsManager;

class DonateDialog : public QDialog
{
    Q_OBJECT

public:
    DonateDialog(QDialog *parent = Q_NULLPTR);
    ~DonateDialog();

public slots:
    void saveAndClose();

private:
    Ui::DonateDialog *ui;
    SettingsManager *settingsManager;

protected:
	void changeEvent(QEvent* event) override;
};

#endif // DONATEDIALOG_HPP
