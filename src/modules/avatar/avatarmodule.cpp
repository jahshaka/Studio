/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/avatar/avatarmodule.h"

#include "bridge/avatarpreview.h"
#include "bridge/enginehost.h"
#include "modules/avatar/api/avatarapi.h"
#include "modules/avatar/avatarpage.h"
#include "modules/avatar/avatarpreviewmodel.h"
#include "modules/avatar/avatarspace.h"
#include "data/settingsmanager.h"
#include "services/assetservice.h"
#include "services/projectassets.h"

#include <QSet>
#include "services/services.h"
#include "scripting/scriptengine.h"
#include "ui/ishellview.h"

#include <QMessageBox>

AvatarModule::AvatarModule() = default;
AvatarModule::~AvatarModule() = default;

void AvatarModule::initialize(StudioContext &host)
{
    this->host = host;
    mModel.reset(new avatar::AvatarPreviewModel());

    // The persisted space choice (AVATAR_SPACE_SPEC): apply before the page
    // exists so the first frame already shows the right environment.
    if (host.settings) {
        avatar::SpaceMode m;
        const QString saved = host.settings->getValue("avatar/space", "modern").toString();
        if (avatar::space::parseMode(saved, &m)) mModel->setSpaceMode(m);
    }
    mPage = new avatar::AvatarPage(mModel.get(), host.shellWidget);

    // The centre view is engine-side (src/bridge/) and only exists when the
    // engine runs — the module itself links no engine code, exactly as the
    // materials module gets its Display preview.
    if (host.engine && host.engine->isRunning()) {
        mPreview = new AvatarPreview(host.engine->engine(), host.engine->driver(), mPage);
        mPage->setPreviewWidget(mPreview);
    }
}

void AvatarModule::contribute(Contributions &c)
{
    c.setPage(mPage);
    c.opensAssetKind(QStringLiteral("avatar"));
}

bool AvatarModule::openAsset(const AssetRef &ref)
{
    if (ref.kind != QLatin1String("avatar")) return false;
    QWidget *parent = host.shellWidget;
    switch (ref.intent) {
    case AssetRef::Intent::Open: {
        // THE PAGE -> MODULE SEAM (AVATAR_ASSET_SPEC §5.5): the space first,
        // then the verb.
        if (host.shell) host.shell->setSpace(id());
        if (!mApi) return true;
        QVariantMap options;
        if (!ref.scope.isEmpty()) options.insert(QStringLiteral("scope"), ref.scope);
        auto *api = mApi;
        const QVariantMap opened = api->quietly([&] { return api->open(ref.guid, options); });
        // A refusal is the module's own message (a definition that will not
        // parse, a project scope with nothing pinned).
        if (opened.isEmpty() && !api->lastError().isEmpty())
            QMessageBox::warning(parent, QObject::tr("Edit in Avatar Module"), api->lastError());
        return true;
    }
    case AssetRef::Intent::Spawn: {
        if (!mApi) return true;
        auto *api = mApi;
        QVariantMap options;
        if (ref.hasPosition)
            options.insert(QStringLiteral("position"),
                           QVariantMap{ { "x", ref.position[0] }, { "y", ref.position[1] },
                                        { "z", ref.position[2] } });
        if (api->quietly([&] { return api->spawn(ref.guid, options); }).isEmpty()
            && !api->lastError().isEmpty())
            QMessageBox::warning(parent, QObject::tr("Add Avatar to Scene"), api->lastError());
        return true;
    }
    case AssetRef::Intent::Assign: {
        // NOTHING UNDER THE CURSOR (R2, 2026-09-11: an Animation tile dropped in
        // the viewport did nothing at all, with no message). A clip is not a
        // scene object — it is something a character wears — so the drop says
        // that.
        if (ref.targetGuid.isEmpty()) {
            if (host.shell)
                host.shell->showViewportToast(QObject::tr("Animation"),
                    QObject::tr("Drop an animation onto a character to assign the clip"));
            return true;
        }
        if (!mApi) return true;
        auto *api = mApi;
        const QVariantMap result = api->quietly(
            [&] { return api->loadClip(ref.targetGuid, ref.guid, QVariantMap()); });
        if (!host.shell) return true;
        if (result.isEmpty()) {
            host.shell->showViewportToast(QObject::tr("Animation"),
                api->lastError().isEmpty()
                    ? QObject::tr("'%1' cannot take this clip").arg(ref.targetName)
                    : api->lastError());
            return true;
        }
        const QVariantList added = result.value(QStringLiteral("clips")).toList();
        host.shell->showViewportToast(QObject::tr("Animation"),
            QObject::tr("%1 clip(s) added to %2").arg(added.size()).arg(ref.targetName));
        return true;
    }
    }
    return false;
}

void AvatarModule::registerApi(ScriptEngine &engine)
{
    mApi = new AvatarApi(engine.scriptHost(), mModel.get());
    if (mPreview) {
        auto *preview = mPreview;
        mApi->setSnapshotDelegate([preview](int w, int h) { return preview->renderPreview(w, h); });
        mApi->setPoseResolver([preview]() { preview->resolvePose(); });
    }
    if (mPage) {
        auto *page = mPage;
        // The page's project actions, routed through the services the module
        // WAS given (StudioContext) — the page never reaches for them itself.
        auto host_ = host;
        auto *api = mApi;
        QObject::connect(page, &avatar::AvatarPage::addAvatarToProject, page,
                         [host_, page](const QString &guid) {
            if (!host_.db || !host_.project) return;
            ProjectAssets::addToProject(guid, host_.db, host_.project,
                                        ProjectAssets::AddKind::Direct);
            page->refreshFromModel();
        });
        QObject::connect(page, &avatar::AvatarPage::updateAvatarFromLibrary, page,
                         [host_, page](const QString &guid) {
            if (!host_.db || !host_.project) return;
            if (!ProjectAssets::updatePinToLatest(guid, host_.db, host_.project)) return;
            if (host_.services && host_.services->assets)
                host_.services->assets->announcePinChanged(guid);
            page->refreshFromModel();
        });
        QObject::connect(page, &avatar::AvatarPage::addAvatarToScene, page,
                         [api](const QString &guid) {
            if (api) api->spawn(guid);
        });
        // The page is a VIEW over the verbs: whoever calls one — a button, the
        // console, an MCP session — the widgets re-read the model afterwards.
        mApi->setChangedDelegate([page]() { page->refreshFromModel(); });
        mPage->setApi(mApi);
    }
    if (host.settings) {
        auto *settings = host.settings;
        mApi->setPersistModeDelegate([settings](const char *name) {
            settings->setValue("avatar/space", QString::fromLatin1(name));
        });
    }
    if (mPreview) {
        auto *preview = mPreview;
        // Re-frame ONLY when the subject changed; doing it on every state
        // change would fight the user's orbit on every scrub.
        mApi->setSubjectDelegate([preview]() { preview->framePreview(); });
    }
    // THE PIN-CHANGE SUBSCRIPTION (AVATAR_ASSET_SPEC §4 D4). Every move of a
    // project's pin — add to project, update from library, a copy-on-write
    // save — changes which bytes this project's instances are made of, so
    // linked avatar instances re-resolve on the spot instead of at the next
    // scene open. The API module is owned by the ScriptEngine, which outlives
    // this subscription's publisher for the life of the session.
    if (host.services && host.services->assets) {
        auto *api = mApi;
        host.services->assets->onPinChanged(
            [api](const QString &assetGuid) { api->onAssetPinChanged(assetGuid); });
    }
    // A SCENE CAN BE STALE THE MOMENT IT LOADS: an instance records the
    // definition version it last resolved, and a scene saved before a module
    // save comes back naming an older one. The pin signal above covers "the
    // asset changed while the scene was open"; this covers the other end.
    if (host.services) {
        auto *api = mApi;
        host.services->onSceneOpened([api]() { api->onSceneOpened(); });
    }
    engine.addModule(mApi);
}

void AvatarModule::abortBackgroundWork()
{
    // The shell calls this at the TOP of its teardown, before it waits on the
    // global thread pool — which is where this module's import worker lives.
    if (mApi) mApi->abortBackgroundWork();
}

void AvatarModule::shutdown()
{
    // The page (and with it the preview widget) belongs to the stacked widget;
    // the API module belongs to the ScriptEngine. Only the document model is
    // ours, and it must go before the engine does.
    if (mPreview) mPreview->setPreviewModel(nullptr);
    if (mPage) mPage->detachModel();   // stop the 100ms ticker + null its raw pointer
    // AV1: the API may have an import worker or a preview parse in flight, and
    // both end by touching the model we are about to free (the import.shutdown
    // zombie class). detachModel stops and joins them first.
    if (mApi) mApi->detachModel();
    mModel.reset();
}
