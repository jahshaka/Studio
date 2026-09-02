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
//   (session history)  |     (engine centre view)  |   file, bones, meshes...
//   [ Load... ]        |  transport + clip + scrub |  ANIMATIONS
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

private:
    QWidget *buildLeftColumn();
    QWidget *buildCentreColumn();
    QWidget *buildRightColumn();
    void onLoadClicked();
    void loadPath(const QString &path);
    void rememberPath(const QString &path);
    void refreshTransportReadout();

    AvatarPreviewModel   *mModel = nullptr;
    IAvatarPreviewWidget *mPreview = nullptr;
    AvatarApi            *mApi = nullptr;

    QWidget     *mPreviewSlot = nullptr;
    QLabel      *mPreviewPlaceholder = nullptr;
    QListWidget *mHistory = nullptr;
    QPushButton *mLoadButton = nullptr;
    QCheckBox   *mMeshToggle = nullptr;
    QCheckBox   *mSkeletonToggle = nullptr;
    QPushButton *mPlayButton = nullptr;
    QPushButton *mPauseButton = nullptr;
    QPushButton *mStopButton = nullptr;
    QCheckBox   *mLoopToggle = nullptr;
    QComboBox   *mClipCombo = nullptr;
    QSlider     *mScrub = nullptr;
    QLabel      *mTimeLabel = nullptr;
    QLabel      *mDetails = nullptr;
    QTreeWidget *mAnimations = nullptr;

    QTimer      *mTicker = nullptr;   // follows the clock while playing (label + scrub only)
    QStringList mSessionPaths;
    bool mUpdating = false;
};

} // namespace avatar

#endif // AVATARPAGE_H
