#ifndef POSTPROCESSESWIDGET_H
#define POSTPROCESSESWIDGET_H

#include <QWidget>
#include "../irisgl/src/irisglfwd.h"

namespace Ui {
class PostProcessesWidget;
}

class PostProcessesWidget : public QWidget
{
    Q_OBJECT



public:
    explicit PostProcessesWidget(QWidget *parent = 0);
    ~PostProcessesWidget();

    void clear();
    void clearLayout(QLayout *layout);
    void setPostProcessMgr(const iris::PostProcessManagerPtr &value);

private slots:
    void addPostProcess(QAction* name);

private:
    Ui::PostProcessesWidget *ui;
    QList<iris::PostProcessPtr> postProcesses;
    iris::PostProcessManagerPtr postProcessMgr;
};

#endif // POSTPROCESSESWIDGET_H
