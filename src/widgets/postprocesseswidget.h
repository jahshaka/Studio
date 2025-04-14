/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
