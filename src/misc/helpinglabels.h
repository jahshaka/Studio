#ifndef HELPINGLABELS_H
#define HELPINGLABELS_H

#include <QWidget>

namespace Ui {
class HelpingLabels;
}

class HelpingLabels : public QWidget
{
    Q_OBJECT

public:
    explicit HelpingLabels(QWidget *parent = 0);
    ~HelpingLabels();
	Ui::HelpingLabels *ui;

private:

protected:
	void changeEvent(QEvent *event) override;
   
};

#endif // HELPINGLABELS_H
