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
#include <QComboBox>
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


void AvatarPage::detachModel()
{
    if (mTicker) mTicker->stop();
    mModel = nullptr;
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

    // THE LIBRARY LIST (AVATAR_ASSET_SPEC §5.3), two sections. LIBRARY is the
    // store's avatar assets; PROJECT is this project's own versions of them,
    // marked [edited] when the pin has diverged from the library's current
    // version. That marking IS the owner's model made visible: asset edits go
    // to the asset, project edits go to the project, and the user can always
    // see which one they are looking at.
    mLibrary = new QTreeWidget(column);
    mLibrary->setColumnCount(2);
    mLibrary->setHeaderLabels({ tr("Avatar"), tr("Version") });
    mLibrary->setRootIsDecorated(true);
    mLibrary->header()->setStretchLastSection(true);
    mLibrary->setToolTip(tr("Double-click to edit; right-click for project actions"));
    connect(mLibrary, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        const QString guid = item->data(0, Qt::UserRole).toString();
        if (guid.isEmpty()) return;   // a section header
        openSelected(guid, item->data(0, Qt::UserRole + 1).toString());
    });
    mLibrary->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mLibrary, &QTreeWidget::customContextMenuRequested,
            this, &AvatarPage::showLibraryMenu);
    layout->addWidget(mLibrary, 1);

    // EVERY LOAD IS AN IMPORT (D7): a file picked here goes through the one
    // import pipeline and becomes a library row, not a session entry that dies
    // with the process. The second button does the same and pins it, so
    // "bring this character into my project" is one gesture rather than three.
    mImportButton = new QPushButton(tr("Import Avatar..."), column);
    connect(mImportButton, &QPushButton::clicked, this, [this]() { onImportClicked(false); });
    layout->addWidget(mImportButton);

    mImportToProjectButton = new QPushButton(tr("Import to Project..."), column);
    mImportToProjectButton->setToolTip(tr("Import the file AND add it to the open project, in "
                                          "one step"));
    connect(mImportToProjectButton, &QPushButton::clicked, this, [this]() { onImportClicked(true); });
    layout->addWidget(mImportToProjectButton);
    return column;
}

QString AvatarPage::selectedAvatarGuid(QString *scopeOut) const
{
    if (!mLibrary) return QString();
    auto *item = mLibrary->currentItem();
    if (!item) return QString();
    if (scopeOut) *scopeOut = item->data(0, Qt::UserRole + 1).toString();
    return item->data(0, Qt::UserRole).toString();
}

void AvatarPage::openSelected(const QString &guid, const QString &scope)
{
    if (!mApi || guid.isEmpty()) return;
    QVariantMap options;
    if (!scope.isEmpty()) options.insert(QStringLiteral("scope"), scope);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVariantMap opened = mApi->quietly([&] { return mApi->open(guid, options); });
    QApplication::restoreOverrideCursor();
    if (opened.isEmpty() && !mApi->lastError().isEmpty())
        QMessageBox::warning(this, tr("Open Avatar"), mApi->lastError());
    refreshFromModel();
}

void AvatarPage::showLibraryMenu(const QPoint &pos)
{
    if (!mApi || !mLibrary) return;
    auto *item = mLibrary->itemAt(pos);
    if (!item) return;
    const QString guid = item->data(0, Qt::UserRole).toString();
    if (guid.isEmpty()) return;
    const QString scope = item->data(0, Qt::UserRole + 1).toString();
    const bool isProject = scope == QLatin1String("project");
    const bool edited = item->data(0, Qt::UserRole + 2).toBool();

    QMenu menu(this);
    QAction *edit = menu.addAction(tr("Edit"));
    QAction *addToProject = isProject ? nullptr : menu.addAction(tr("Add to Project"));
    QAction *addToScene = isProject ? menu.addAction(tr("Add to Scene")) : nullptr;
    // Only offered when it would DO something: a pin already on the library's
    // current version has nothing to take.
    QAction *update = (isProject && edited) ? menu.addAction(tr("Update from Library")) : nullptr;
    QAction *publish = isProject ? menu.addAction(tr("Save to Library")) : nullptr;

    QAction *chosen = menu.exec(mLibrary->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == edit) { openSelected(guid, scope); return; }
    if (addToProject && chosen == addToProject) {
        // The generic verb, not an avatar-shaped copy of it: an avatar is
        // pinned exactly like every other asset (§4 D3).
        emit addAvatarToProject(guid);
        refreshFromModel();
        return;
    }
    if (addToScene && chosen == addToScene) { emit addAvatarToScene(guid); return; }
    if (update && chosen == update) {
        if (QMessageBox::question(
                this, tr("Update from Library"),
                tr("Replace this project's version of the avatar with the library's current "
                   "one?\n\nAny edits made to it inside this project will no longer be used."))
            == QMessageBox::Yes)
            emit updateAvatarFromLibrary(guid);
        refreshFromModel();
        return;
    }
    if (publish && chosen == publish) {
        const QVariantMap result = mApi->quietly([&] { return mApi->saveToLibrary(guid); });
        if (result.isEmpty() && !mApi->lastError().isEmpty())
            QMessageBox::warning(this, tr("Save to Library"), mApi->lastError());
        refreshFromModel();
    }
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

    // The space switcher (AVATAR_SPACE_SPEC): the dropdown is a view over
    // avatar.spaceMode, like every other control here.
    mSpaceCombo = new QComboBox(toggles);
    mSpaceCombo->addItem(tr("Grid"), QStringLiteral("grid"));
    mSpaceCombo->addItem(tr("Modern"), QStringLiteral("modern"));
    connect(mSpaceCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (mUpdating || !mApi || index < 0) return;
        mApi->spaceMode(mSpaceCombo->itemData(index).toString());
    });
    toggleLayout->addWidget(mSpaceCombo);
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

    // WHICH SCOPE IS OPEN, and whether it has unsaved edits. The module edits
    // EITHER a library asset OR the project's version of one, and the owner's
    // rule ("asset edits go to the asset, project edits go to the project")
    // only works if the user can see which one they have.
    mScopeLabel = new QLabel(column);
    mScopeLabel->setWordWrap(true);
    mScopeLabel->setObjectName("avatarScopeLabel");
    layout->addWidget(mScopeLabel);

    mSaveButton = new QPushButton(tr("Save"), column);
    mSaveButton->setToolTip(tr("Writes the definition back to the scope it was opened from"));
    connect(mSaveButton, &QPushButton::clicked, this, &AvatarPage::onSaveClicked);
    layout->addWidget(mSaveButton);

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
        mApi->setClip(item->data(0, Qt::UserRole).toString());
        refreshFromModel();
    });
    // With an avatar OPEN the rows are the DEFINITION's clips, so the menu
    // edits the definition (and marks it dirty) rather than the preview: the
    // module edits an asset, and this is where the clip half of one is edited.
    mAnimations->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mAnimations, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        if (!mApi) return;
        auto *item = mAnimations->itemAt(pos);
        if (!item) return;
        const QVariantMap open = mApi->asset();
        if (open.isEmpty()) return;   // inspection-only preview: nothing to edit
        const QString name = item->data(0, Qt::UserRole).toString();
        const bool looping = item->data(0, Qt::UserRole + 1).toBool();
        const bool rootMotion = item->data(0, Qt::UserRole + 2).toBool();
        const bool isDefault = item->data(0, Qt::UserRole + 3).toBool();

        QMenu menu(this);
        QAction *loop = menu.addAction(tr("Looping"));
        loop->setCheckable(true);
        loop->setChecked(looping);
        QAction *root = menu.addAction(tr("Root Motion"));
        root->setCheckable(true);
        root->setChecked(rootMotion);
        QAction *makeDefault = menu.addAction(tr("Set as Default"));
        makeDefault->setEnabled(!isDefault);
        menu.addSeparator();
        QAction *remove = menu.addAction(tr("Remove"));

        QAction *chosen = menu.exec(mAnimations->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        mApi->quietly([&] {
            if (chosen == loop)
                mApi->setClipOptions(name, { { QStringLiteral("looping"), !looping } });
            else if (chosen == root)
                mApi->setClipOptions(name, { { QStringLiteral("rootMotion"), !rootMotion } });
            else if (chosen == makeDefault)
                mApi->setDefaultClip(name);
            else if (chosen == remove)
                mApi->removeClip(name);
            return true;
        });
        if (!mApi->lastError().isEmpty())
            QMessageBox::warning(this, tr("Clip"), mApi->lastError());
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

void AvatarPage::onImportClicked(bool intoProject)
{
    if (!mApi) return;
    QStringList filters;
    for (const auto &ext : Constants::MODEL_EXTS) filters.append("*." + ext);
    const QString path = QFileDialog::getOpenFileName(
        this, intoProject ? tr("Import an avatar into the project") : tr("Import an avatar"),
        QString(), tr("Models (%1)").arg(filters.join(' ')));
    if (path.isEmpty()) return;

    // R0.11: the assimp parse is synchronous on the UI thread — a large FBX
    // freezes the page for seconds. The busy cursor is the honest stopgap; the
    // threaded ImportBatchRunner is a later problem if it becomes one.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QVariantMap options;
    if (intoProject) options.insert(QStringLiteral("scope"), QStringLiteral("project"));
    const QVariantMap result = mApi->quietly([&] { return mApi->importAvatar(path, options); });
    QApplication::restoreOverrideCursor();

    const QString avatarGuid = result.value(QStringLiteral("avatar")).toString();
    if (avatarGuid.isEmpty()) {
        QMessageBox::warning(this, tr("Import Avatar"),
                             mApi->lastError().isEmpty()
                                 ? tr("That file could not be imported as an avatar.")
                                 : mApi->lastError());
        refreshFromModel();
        return;
    }
    // The verb already opened it (D7-A) — the page just catches up.
    refreshFromModel();
}

void AvatarPage::onSaveClicked()
{
    if (!mApi) return;
    const QVariantMap result = mApi->quietly([&] { return mApi->save(); });
    if (result.isEmpty() && !mApi->lastError().isEmpty())
        QMessageBox::warning(this, tr("Save Avatar"), mApi->lastError());
    refreshFromModel();
}

void AvatarPage::onLoadAnimationClicked()
{
    if (!mApi || !mModel) return;
    if (!mModel->isLoaded()) {
        QMessageBox::information(this, tr("Load Animation"),
                                 tr("Load a character first — an animation needs a rig to play on."));
        return;
    }
    // ANIMATION_EXTS, not MODEL_EXTS: a mocap .bvh has no geometry, so it is
    // loadable HERE (clips only) and nowhere else in the app.
    QStringList filters;
    for (const auto &ext : Constants::ANIMATION_EXTS) filters.append("*." + ext);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load an animation"), QFileInfo(mModel->filePath()).absolutePath(),
        tr("Animations (%1)").arg(filters.join(' ')));
    if (path.isEmpty()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVariant result = mApi->quietly([&] { return mApi->loadAnimation(path); });
    QApplication::restoreOverrideCursor();
    // A rig mismatch is a REFUSAL, not a silent no-op: the verb throws, and
    // the message names the bones the loaded rig does not have.
    if (!result.isValid())
        QMessageBox::warning(this, tr("Load Animation"), mApi->lastError().isEmpty()
                                 ? tr("That file could not be loaded as an animation.")
                                 : mApi->lastError());
    refreshFromModel();
}

void AvatarPage::refreshLibrary()
{
    if (!mLibrary || !mApi) return;
    const QString openGuid = mApi->asset().value(QStringLiteral("guid")).toString();
    const QString openScope = mApi->asset().value(QStringLiteral("scope")).toString();

    mLibrary->clear();
    auto *libraryRoot = new QTreeWidgetItem(mLibrary, { tr("LIBRARY"), QString() });
    libraryRoot->setFirstColumnSpanned(true);
    QTreeWidgetItem *projectRoot = nullptr;

    for (const QVariant &row : mApi->library()) {
        const QVariantMap avatarRow = row.toMap();
        const QString scope = avatarRow.value(QStringLiteral("scope")).toString();
        const bool isProject = scope == QLatin1String("project");
        if (isProject && !projectRoot) {
            projectRoot = new QTreeWidgetItem(mLibrary, { tr("PROJECT"), QString() });
            projectRoot->setFirstColumnSpanned(true);
        }
        const bool edited = avatarRow.value(QStringLiteral("edited")).toBool();
        const QString version = avatarRow.value(QStringLiteral("version")).toString();
        auto *item = new QTreeWidgetItem(isProject ? projectRoot : libraryRoot);
        item->setText(0, avatarRow.value(QStringLiteral("name")).toString()
                         + (isProject && edited ? tr("  [edited]") : QString()));
        // The version is a content id, so the first eight hex digits are what a
        // person can actually compare between two rows.
        item->setText(1, version.left(8));
        item->setToolTip(1, version);
        item->setData(0, Qt::UserRole, avatarRow.value(QStringLiteral("guid")));
        item->setData(0, Qt::UserRole + 1, scope);
        item->setData(0, Qt::UserRole + 2, edited);
        const QVariantList clips = avatarRow.value(QStringLiteral("clips")).toList();
        item->setToolTip(0, tr("bones: %1 · clips: %2")
                                .arg(avatarRow.value(QStringLiteral("bones")).toInt())
                                .arg(clips.size()));
        if (avatarRow.value(QStringLiteral("guid")).toString() == openGuid && scope == openScope)
            mLibrary->setCurrentItem(item);
    }
    mLibrary->expandAll();
    libraryRoot->setHidden(libraryRoot->childCount() == 0);
}

void AvatarPage::refreshFromModel()
{
    if (!mModel) return;
    mUpdating = true;

    const bool loaded = mModel->isLoaded();
    mMeshToggle->setChecked(mModel->meshVisible());
    mSkeletonToggle->setChecked(mModel->skeletonVisible());
    mLoopToggle->setChecked(mModel->looping());
    if (mSpaceCombo)
        mSpaceCombo->setCurrentIndex(
            mModel->spaceMode() == avatar::SpaceMode::Modern ? 1 : 0);
    mRootMotionToggle->setChecked(mModel->rootMotion());
    const QWidget *const transport[] = { mPlayButton, mPauseButton, mStopButton,
                                         mLoopToggle, mRootMotionToggle, mScrub };
    for (const QWidget *w : transport) const_cast<QWidget *>(w)->setEnabled(loaded);
    mLoadAnimButton->setEnabled(loaded);

    refreshLibrary();

    const auto clips = mModel->clips();
    // WITH AN AVATAR OPEN the rows are the DEFINITION's clips — the avatar's
    // authored clip list, which is what a spawned instance will play — and the
    // preview's lengths are joined in by name. Without one (the inspection
    // path) they are the preview's, exactly as before.
    const QVariantMap openAsset = mApi ? mApi->asset() : QVariantMap();
    const QVariantList definitionClips =
        openAsset.value(QStringLiteral("definition")).toMap()
            .value(QStringLiteral("clips")).toList();
    const QString defaultClip = openAsset.value(QStringLiteral("definition")).toMap()
                                    .value(QStringLiteral("defaultClip")).toString();
    QMap<QString, float> lengthByName;
    QString activeClip;
    for (const auto &clip : clips) {
        lengthByName.insert(clip.name, clip.length);
        if (clip.active) activeClip = clip.name;
    }

    mAnimations->clear();
    if (!definitionClips.isEmpty()) {
        for (const QVariant &row : definitionClips) {
            const QVariantMap clip = row.toMap();
            const QString name = clip.value(QStringLiteral("name")).toString();
            auto *item = new QTreeWidgetItem(mAnimations);
            const bool isDefault = name == defaultClip;
            item->setText(0, isDefault ? name + tr("  (default)") : name);
            item->setText(1, lengthByName.contains(name)
                                 ? QString::number(lengthByName.value(name), 'f', 2) + " s"
                                 // A clip the definition names but the preview
                                 // has not loaded is not an error: it is a
                                 // library clip whose bytes are only fetched
                                 // when an instance plays it.
                                 : tr("—"));
            item->setToolTip(0, tr("in the file: %1")
                                    .arg(clip.value(QStringLiteral("rawName")).toString()));
            item->setData(0, Qt::UserRole, name);
            item->setData(0, Qt::UserRole + 1, clip.value(QStringLiteral("looping")));
            item->setData(0, Qt::UserRole + 2, clip.value(QStringLiteral("rootMotion")));
            item->setData(0, Qt::UserRole + 3, isDefault);
            if (name == activeClip) mAnimations->setCurrentItem(item);
        }
    } else {
        for (const auto &clip : clips) {
            auto *item = new QTreeWidgetItem(mAnimations);
            item->setText(0, clip.name);
            item->setText(1, QString::number(clip.length, 'f', 2) + " s");
            // A cross-file clip says which file it came from; a same-file one
            // says what the file called it (every Mixamo clip says "mixamo.com").
            item->setToolTip(0, clip.external
                                    ? tr("from %1 (in the file: %2)")
                                          .arg(QFileInfo(clip.source).fileName(), clip.rawName)
                                    : tr("in the file: %1").arg(clip.rawName));
            item->setData(0, Qt::UserRole, clip.name);
            if (clip.active) mAnimations->setCurrentItem(item);
        }
    }

    const float duration = mModel->duration();
    mScrub->setValue(duration > 0.0f ? int(mModel->time() / duration * kScrubSteps) : 0);
    mTimeLabel->setText(QString("%1 / %2 s").arg(mModel->time(), 0, 'f', 2).arg(duration, 0, 'f', 2));

    // Only what is loadable today (§0.8): no skinning mode (GPU_SKINNING's
    // verb), no controller block (Part 2), no source asset guid (Part 1 —
    // there is no library row).
    // The scope banner + Save. Enabled only when there IS something to save,
    // so the button never lies about having work to do.
    if (mScopeLabel && mSaveButton) {
        if (openAsset.isEmpty()) {
            mScopeLabel->setText(tr("No avatar open."));
            mSaveButton->setEnabled(false);
        } else {
            const bool dirty = openAsset.value(QStringLiteral("dirty")).toBool();
            const QString scope = openAsset.value(QStringLiteral("scope")).toString();
            mScopeLabel->setText(
                tr("<b>%1</b> — editing the %2 version%3")
                    .arg(openAsset.value(QStringLiteral("name")).toString().toHtmlEscaped(),
                         scope == QLatin1String("project") ? tr("PROJECT's") : tr("LIBRARY"),
                         dirty ? tr(" · <i>unsaved changes</i>") : QString()));
            mSaveButton->setEnabled(dirty);
        }
    }

    if (!loaded) {
        mDetails->setText(tr("Nothing loaded. Import an avatar, or pick one from the list."));
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
