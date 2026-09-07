/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIGHTCHANNELSWIDGET_H
#define LIGHTCHANNELSWIDGET_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;

/// LIGHTING CHANNELS, as a compact row of eight checkboxes.
///
/// The document field is 32 bits wide (iris::SceneNode::lightMask) and scripts
/// may use all of them; this row edits the low EIGHT, which is what a human
/// needs and what Unreal offers. The upper 24 are PRESERVED, never cleared, and
/// the widget says so out loud when any of them is set — a panel that silently
/// dropped bits a script had written would be worse than no panel.
///
/// One control for both ends of the feature: on a LIGHT the checked channels
/// are the ones it illuminates, on an OBJECT they are the ones it is lit by,
/// and the renderer lights the object when the two sets intersect. The caller
/// passes the wording that fits (`setDescription`).
class LightChannelsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LightChannelsWidget(QWidget *parent = nullptr);

    /// Sets the displayed mask without emitting maskChanged.
    void setMask(quint32 mask);
    quint32 mask() const { return mMask; }

    /// The one-line explanation under the boxes ("this light lights..." vs
    /// "this object is lit by...").
    void setDescription(const QString &text);

signals:
    void maskChanged(quint32 mask);

private:
    void refresh();
    void emitMask(quint32 mask);

    quint32 mMask = 0xFFFFFFFFu;
    QCheckBox *mBoxes[8] = {};
    QPushButton *mAll = nullptr;
    QPushButton *mNone = nullptr;
    QLabel *mNote = nullptr;
    QLabel *mDescription = nullptr;
};

#endif  // LIGHTCHANNELSWIDGET_H
