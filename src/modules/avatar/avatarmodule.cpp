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
#include "scripting/scriptengine.h"

AvatarModule::AvatarModule() = default;
AvatarModule::~AvatarModule() = default;

void AvatarModule::initialize(ModuleHost &host)
{
    this->host = host;
    mModel.reset(new avatar::AvatarPreviewModel());
    mPage = new avatar::AvatarPage(mModel.get(), host.shellWidget);

    // The centre view is engine-side (src/bridge/) and only exists when the
    // engine runs — the module itself links no engine code, exactly as the
    // materials module gets its Display preview.
    if (host.engine && host.engine->isRunning()) {
        mPreview = new AvatarPreview(host.engine->engine(), host.engine->driver(), mPage);
        mPage->setPreviewWidget(mPreview);
    }
}

QWidget *AvatarModule::createPage()
{
    return mPage;
}

void AvatarModule::registerApi(ScriptEngine &engine)
{
    mApi = new AvatarApi(engine.scriptHost(), mModel.get());
    if (mPreview) {
        auto *preview = mPreview;
        mApi->setSnapshotDelegate([preview](int w, int h) { return preview->renderPreview(w, h); });
    }
    if (mPage) {
        auto *page = mPage;
        // The page is a VIEW over the verbs: whoever calls one — a button, the
        // console, an MCP session — the widgets re-read the model afterwards.
        mApi->setChangedDelegate([page]() { page->refreshFromModel(); });
        mPage->setApi(mApi);
    }
    if (mPreview) {
        auto *preview = mPreview;
        // Re-frame ONLY when the subject changed; doing it on every state
        // change would fight the user's orbit on every scrub.
        mApi->setSubjectDelegate([preview]() { preview->framePreview(); });
    }
    engine.addModule(mApi);
}

void AvatarModule::shutdown()
{
    // The page (and with it the preview widget) belongs to the stacked widget;
    // the API module belongs to the ScriptEngine. Only the document model is
    // ours, and it must go before the engine does.
    if (mPreview) mPreview->setPreviewModel(nullptr);
    mModel.reset();
}
