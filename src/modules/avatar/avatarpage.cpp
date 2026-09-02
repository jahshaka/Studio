/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/avatar/avatarpage.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "data/constants.h"
#include "modules/avatar/api/avatarapi.h"
#include "modules/avatar/avatarpreviewmodel.h"
#include "modules/avatar/avatarpreviewwidget.h"

namespace avatar
{

namespace {
const int kScrubSteps = 1000;
}

AvatarPage::AvatarPage(AvatarPreviewModel *model, QWidget *parent)
    : QWidget(parent), mModel(model)
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(buildLeftColumn());
    splitter->addWidget(buildCentreColumn());
    splitter->addWidget(buildRightColumn());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({ 220, 800, 280 });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(splitter);

    // The transport strip is a VIEW over the verbs, and the verbs are what
    // advance the clock — but nothing calls a verb while the preview widget
    // plays on the render driver's frames, so the readout would freeze at the
    // last scripted value. A cheap ticker follows it (label + scrub only; the
    // rest of the page changes only when a verb is called).
    mTicker = new QTimer(this);
    mTicker->setInterval(100);
    connect(mTicker, &QTimer::timeout, this, &AvatarPage::refreshTransportReadout);
    mTicker->start();

    refreshFromModel();
}

void AvatarPage::refreshTransportReadout()
{
    if (!mModel || !mModel->isLoaded() || mUpdating) return;
    const float duration = mModel->duration();
    const float time = mModel->time();
    mUpdating = true;
    mScrub->setValue(duration > 0.0f ? int(time / duration * kScrubSteps) : 0);
    mTimeLabel->setText(QString("%1 / %2 s").arg(time, 0, 'f', 2).arg(duration, 0, 'f', 2));
    mPlayButton->setEnabled(!mModel->isPlaying());
    mPauseButton->setEnabled(mModel->isPlaying());
    mUpdating = false;
}

QWidget *AvatarPage::buildLeftColumn()
{
    auto *column = new QWidget(this);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(tr("AVATARS"), column);
    title->setObjectName("avatarLibraryTitle");
    layout->addWidget(title);

    // D0.4 A: Load... plus a session history. The library list (assets.list
    // filtered on metadata.hasSkeleton) is Part 1's — there is no skeleton
    // metadata to filter on yet.
    mHistory = new QListWidget(column);
    mHistory->setToolTip(tr("Files loaded in this session"));
    connect(mHistory, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item) loadPath(item->data(Qt::UserRole).toString());
    });
    // Right-click Delete drops the row (avatar.forget) — the session list is
    // module state, so removing a row deletes nothing on disk. Clearing the
    // preview when it was the loaded file is the verb's business, not the
    // widget's.
    mHistory->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mHistory, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = mHistory->itemAt(pos);
        if (!item || !mApi) return;
        QMenu menu(this);
        QAction *remove = menu.addAction(tr("Delete"));
        if (menu.exec(mHistory->viewport()->mapToGlobal(pos)) != remove) return;
        mApi->forget(item->data(Qt::UserRole).toString());
        refreshFromModel();
    });
    layout->addWidget(mHistory, 1);

    mLoadButton = new QPushButton(tr("Load..."), column);
    connect(mLoadButton, &QPushButton::clicked, this, &AvatarPage::onLoadClicked);
    layout->addWidget(mLoadButton);
    return column;
}

QWidget *AvatarPage::buildCentreColumn()
{
    auto *column = new QWidget(this);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);

    // The two INDEPENDENT toggles (§0.7) — not three exclusive modes: all four
    // combinations are valid, and skeleton-on does not need the mesh.
    auto *toggles = new QWidget(column);
    auto *toggleLayout = new QHBoxLayout(toggles);
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    mMeshToggle = new QCheckBox(tr("Mesh"), toggles);
    mSkeletonToggle = new QCheckBox(tr("Skeleton"), toggles);
    connect(mMeshToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (mUpdating || !mApi) return;
        mApi->setMeshVisible(on);
    });
    connect(mSkeletonToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (mUpdating || !mApi) return;
        mApi->setSkeletonVisible(on);
    });
    toggleLayout->addWidget(mMeshToggle);
    toggleLayout->addWidget(mSkeletonToggle);
    toggleLayout->addStretch(1);
    layout->addWidget(toggles);

    mPreviewSlot = new QWidget(column);
    auto *slotLayout = new QVBoxLayout(mPreviewSlot);
    slotLayout->setContentsMargins(0, 0, 0, 0);
    mPreviewPlaceholder = new QLabel(tr("The 3D preview needs the engine viewport."), mPreviewSlot);
    mPreviewPlaceholder->setAlignment(Qt::AlignCenter);
    slotLayout->addWidget(mPreviewPlaceholder);
    layout->addWidget(mPreviewSlot, 1);

    // The transport is a VIEW over the verbs, never a second clock: every
    // button calls avatar.*, which drives the module's own preview document.
    // The scene timeline keeps its own clock; they never collide (§8.3).
    //
    // Two rows under the view: the scrub bar full width, then the transport
    // CENTRED. The clip picker is not here — the ANIMATIONS list in the right
    // column is the only clip switcher.
    auto *scrubRow = new QWidget(column);
    auto *scrubLayout = new QHBoxLayout(scrubRow);
    scrubLayout->setContentsMargins(0, 0, 0, 0);
    mScrub = new QSlider(Qt::Horizontal, scrubRow);
    mScrub->setRange(0, kScrubSteps);
    mTimeLabel = new QLabel("0.00 / 0.00 s", scrubRow);
    connect(mScrub, &QSlider::valueChanged, this, [this](int value) {
        if (mUpdating || !mApi || !mModel) return;
        const double duration = mModel->duration();
        if (duration <= 0.0) return;
        mApi->pause();
        mApi->setTime(duration * value / double(kScrubSteps));
    });
    scrubLayout->addWidget(mScrub, 1);
    scrubLayout->addWidget(mTimeLabel);
    layout->addWidget(scrubRow);

    auto *strip = new QWidget(column);
    auto *stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    mPlayButton = new QPushButton(tr("Play"), strip);
    mPauseButton = new QPushButton(tr("Pause"), strip);
    mStopButton = new QPushButton(tr("Stop"), strip);
    mLoopToggle = new QCheckBox(tr("Loop"), strip);
    mRootMotionToggle = new QCheckBox(tr("Root Motion"), strip);
    mRootMotionToggle->setToolTip(tr("Off: locomotion clips play in place. On: the clip's "
                                     "authored travel moves the character."));
    connect(mPlayButton, &QPushButton::clicked, this, [this]() { if (mApi) mApi->playClip(QString()); });
    connect(mPauseButton, &QPushButton::clicked, this, [this]() { if (mApi) mApi->pause(); });
    connect(mStopButton, &QPushButton::clicked, this, [this]() { if (mApi) mApi->stop(); });
    connect(mLoopToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (mUpdating || !mApi) return;
        mApi->setLooping(on);
    });
    connect(mRootMotionToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (mUpdating || !mApi) return;
        mApi->setRootMotion(on);
    });
    stripLayout->addStretch(1);
    stripLayout->addWidget(mPlayButton);
    stripLayout->addWidget(mPauseButton);
    stripLayout->addWidget(mStopButton);
    stripLayout->addSpacing(12);
    stripLayout->addWidget(mLoopToggle);
    stripLayout->addWidget(mRootMotionToggle);
    stripLayout->addStretch(1);
    layout->addWidget(strip);
    return column;
}

QWidget *AvatarPage::buildRightColumn()
{
    auto *column = new QWidget(this);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *detailsTitle = new QLabel(tr("DETAILS"), column);
    layout->addWidget(detailsTitle);
    mDetails = new QLabel(column);
    mDetails->setWordWrap(true);
    mDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mDetails);

    auto *animTitle = new QLabel(tr("ANIMATIONS"), column);
    layout->addWidget(animTitle);
    mAnimations = new QTreeWidget(column);
    mAnimations->setColumnCount(2);
    mAnimations->setHeaderLabels({ tr("Clip"), tr("Length") });
    mAnimations->setRootIsDecorated(false);
    mAnimations->header()->setStretchLastSection(true);
    mAnimations->setToolTip(tr("Double-click a clip to play it on the loaded character"));
    // itemActivated is the double-click (and Enter) — the ONE clip switcher.
    // It carries the DISPLAY name, which is what avatar.setClip takes.
    connect(mAnimations, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (!item || !mApi) return;
        mApi->setClip(item->text(0));
        refreshFromModel();
    });
    layout->addWidget(mAnimations, 1);

    // The Mixamo workflow: the character comes from Load..., every clip after
    // it from here. Same verb a script calls (avatar.loadAnimation).
    mLoadAnimButton = new QPushButton(tr("Load Animation..."), column);
    mLoadAnimButton->setToolTip(tr("Add clips from a separate animation file to the loaded character"));
    connect(mLoadAnimButton, &QPushButton::clicked, this, &AvatarPage::onLoadAnimationClicked);
    layout->addWidget(mLoadAnimButton);
    return column;
}

void AvatarPage::setPreviewWidget(IAvatarPreviewWidget *preview)
{
    mPreview = preview;
    if (!preview || !mPreviewSlot) return;
    if (mPreviewPlaceholder) { mPreviewPlaceholder->hide(); mPreviewPlaceholder->deleteLater(); mPreviewPlaceholder = nullptr; }
    auto *widget = preview->previewWidget();
    widget->setParent(mPreviewSlot);
    mPreviewSlot->layout()->addWidget(widget);
    preview->setPreviewModel(mModel);
}

void AvatarPage::onLoadClicked()
{
    QStringList filters;
    for (const auto &ext : Constants::MODEL_EXTS) filters.append("*." + ext);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load a rigged model"), QString(),
        tr("Models (%1)").arg(filters.join(' ')));
    if (path.isEmpty()) return;
    loadPath(path);
}

void AvatarPage::onLoadAnimationClicked()
{
    if (!mApi || !mModel) return;
    if (!mModel->isLoaded()) {
        QMessageBox::information(this, tr("Load Animation"),
                                 tr("Load a character first — an animation needs a rig to play on."));
        return;
    }
    QStringList filters;
    for (const auto &ext : Constants::MODEL_EXTS) filters.append("*." + ext);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load an animation"), QFileInfo(mModel->filePath()).absolutePath(),
        tr("Animations (%1)").arg(filters.join(' ')));
    if (path.isEmpty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVariant result = mApi->loadAnimation(path);
    QApplication::restoreOverrideCursor();
    // A rig mismatch is a REFUSAL, not a silent no-op: the verb throws, and
    // the message names the bones the loaded rig does not have.
    if (!result.isValid())
        QMessageBox::warning(this, tr("Load Animation"), mApi->lastError().isEmpty()
                                 ? tr("That file could not be loaded as an animation.")
                                 : mApi->lastError());
    refreshFromModel();
}

void AvatarPage::loadPath(const QString &path)
{
    if (!mApi || path.isEmpty()) return;
    // R0.11: the assimp parse is synchronous on the UI thread — a large FBX
    // freezes the page for seconds. The stub accepts that with a busy cursor;
    // the threaded ImportBatchRunner is Part 1's problem if it becomes one.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    mApi->loadPreview(path);
    QApplication::restoreOverrideCursor();
    // Re-framing and the widget refresh ride the verb's own delegates
    // (AvatarModule::registerApi), so a scripted load looks identical.
    refreshFromModel();
}

void AvatarPage::refreshHistory()
{
    // The session list lives in the MODEL (avatar.history/avatar.forget), so a
    // scripted load shows up here exactly like a clicked one.
    const QString current = mModel->filePath();
    mHistory->clear();
    for (const QString &path : mModel->history()) {
        auto *item = new QListWidgetItem(QFileInfo(path).fileName(), mHistory);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        if (path == current) mHistory->setCurrentItem(item);
    }
}

void AvatarPage::refreshFromModel()
{
    if (!mModel) return;
    mUpdating = true;

    const bool loaded = mModel->isLoaded();
    mMeshToggle->setChecked(mModel->meshVisible());
    mSkeletonToggle->setChecked(mModel->skeletonVisible());
    mLoopToggle->setChecked(mModel->looping());
    mRootMotionToggle->setChecked(mModel->rootMotion());
    const QWidget *const transport[] = { mPlayButton, mPauseButton, mStopButton,
                                         mLoopToggle, mRootMotionToggle, mScrub };
    for (const QWidget *w : transport) const_cast<QWidget *>(w)->setEnabled(loaded);
    mLoadAnimButton->setEnabled(loaded);

    refreshHistory();

    const auto clips = mModel->clips();
    mAnimations->clear();
    for (const auto &clip : clips) {
        auto *item = new QTreeWidgetItem(mAnimations);
        item->setText(0, clip.name);
        item->setText(1, QString::number(clip.length, 'f', 2) + " s");
        // A cross-file clip says which file it came from; a same-file one says
        // what the file called it (every Mixamo clip says "mixamo.com").
        item->setToolTip(0, clip.external
                                ? tr("from %1 (in the file: %2)")
                                      .arg(QFileInfo(clip.source).fileName(), clip.rawName)
                                : tr("in the file: %1").arg(clip.rawName));
        if (clip.active) mAnimations->setCurrentItem(item);
    }

    const float duration = mModel->duration();
    mScrub->setValue(duration > 0.0f ? int(mModel->time() / duration * kScrubSteps) : 0);
    mTimeLabel->setText(QString("%1 / %2 s").arg(mModel->time(), 0, 'f', 2).arg(duration, 0, 'f', 2));

    // Only what is loadable today (§0.8): no skinning mode (GPU_SKINNING's
    // verb), no controller block (Part 2), no source asset guid (Part 1 —
    // there is no library row).
    if (!loaded) {
        mDetails->setText(tr("Nothing loaded. Use Load... to open a rigged model."));
    } else {
        mDetails->setText(tr("<b>%1</b><br/>%2<br/><br/>bones: %3<br/>meshes: %4<br/>"
                             "vertices: %5<br/>influences/vertex: %6<br/>clips: %7")
                              .arg(mModel->name().toHtmlEscaped(),
                                   QFileInfo(mModel->filePath()).fileName().toHtmlEscaped())
                              .arg(mModel->boneCount())
                              .arg(mModel->meshCount())
                              .arg(mModel->vertexCount())
                              .arg(mModel->influencesPerVertex())
                              .arg(clips.size()));
    }

    mUpdating = false;
}

} // namespace avatar
