/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARPAGE_H
#define AVATARPAGE_H

// AvatarPage — the Avatar space's stacked page (AVATAR_MODULE_SPEC §0.8).
//
//   AVATARS            |  [x] Mesh  [x] Skeleton   |  DETAILS
//   (session history,  |     (engine centre view)  |   file, bones, meshes...
//    right-click       |  ------- scrub bar ------ |  ANIMATIONS
//    Delete)           |    < transport, centred > |   (double-click plays)
//   [ Load... ]        |                           |  [ Load Animation... ]
//
// The clip list is the RIGHT column and nothing else (the Unreal shape): a
// double-click on a row is what switches the active clip. There is no clip
// combo in the centre — the centre is the view, the scrub bar and the
// transport, in that order.
//
// Everything the widgets do goes through the SAME verbs a script calls: the
// page holds no state of its own beyond widget state, and refreshFromModel()
// re-reads AvatarPreviewModel after any change, whoever made it (API-first
// rule — the UI is a view over the verbs, never a second implementation).
//
// Includes no mainwindow.h, no engine header, no Ogre, no GL. The centre view
// arrives as an IAvatarPreviewWidget the shell injects.

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;
class QTreeWidget;

class AvatarApi;

namespace avatar
{

class AvatarPreviewModel;
class IAvatarPreviewWidget;

class AvatarPage : public QWidget
{
    Q_OBJECT
public:
    AvatarPage(AvatarPreviewModel *model, QWidget *parent = nullptr);

    /// The engine-rendered centre view, injected by the shell when the engine
    /// runs. Headless sessions get a placeholder label instead.
    void setPreviewWidget(IAvatarPreviewWidget *preview);
    /// The verbs the widgets drive. Set by the module after registerApi.
    void setApi(AvatarApi *api) { mApi = api; }

    /// Re-reads the model into every widget. Called after each verb.
    void refreshFromModel();
    /// The module is about to free the model (shutdown step 3). The page's
    /// mModel is a RAW pointer and mTicker fires every 100 ms — without this,
    /// any tick between module shutdown and widget-tree death dereferenced the
    /// freed model (intermittent exit crash, 1-in-5 full-suite runs, found by
    /// the AI fix wave's gate B).
    void detachModel();

private:
    QWidget *buildLeftColumn();
    QWidget *buildCentreColumn();
    QWidget *buildRightColumn();
    void onLoadClicked();
    void onLoadAnimationClicked();
    void loadPath(const QString &path);
    void refreshHistory();
    void refreshTransportReadout();


    AvatarPreviewModel   *mModel = nullptr;
    IAvatarPreviewWidget *mPreview = nullptr;
    AvatarApi            *mApi = nullptr;

    QWidget     *mPreviewSlot = nullptr;
    QLabel      *mPreviewPlaceholder = nullptr;
    QListWidget *mHistory = nullptr;
    QPushButton *mLoadButton = nullptr;
    QPushButton *mLoadAnimButton = nullptr;
    QComboBox   *mSpaceCombo = nullptr;
    QCheckBox   *mMeshToggle = nullptr;
    QCheckBox   *mSkeletonToggle = nullptr;
    QPushButton *mPlayButton = nullptr;
    QPushButton *mPauseButton = nullptr;
    QPushButton *mStopButton = nullptr;
    QCheckBox   *mLoopToggle = nullptr;
    QCheckBox   *mRootMotionToggle = nullptr;
    QSlider     *mScrub = nullptr;
    QLabel      *mTimeLabel = nullptr;
    QLabel      *mDetails = nullptr;
    QTreeWidget *mAnimations = nullptr;

    QTimer      *mTicker = nullptr;   // follows the clock while playing (label + scrub only)
    bool mUpdating = false;
};

} // namespace avatar

#endif // AVATARPAGE_H
