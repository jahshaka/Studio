/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/vrmodule.h"

#include "bridge/enginehost.h"
#include "modules/vr/vrapi.h"
#include "scripting/scriptengine.h"
#include "viewport/enginerenderdriver.h"

void VrModule::registerApi(ScriptEngine &engine)
{
    // KEPT, WEAKLY. The ScriptEngine owns the module from here; this pointer is
    // only so shutdown() can end a session through the object that started it
    // (VR-4-FIX finding 7) rather than behind its back — and a QPointer because
    // the scripting engine may well be torn down first.
    api = new VrApi(engine.scriptHost(), host);
    engine.addModule(api);
}

void VrModule::shutdown()
{
    // A SESSION MUST NOT OUTLIVE THE SHELL. It holds a View on the editor's
    // scene, a second workspace on the viewport's target and a set of XR
    // swapchains; the engine's own destructor ends one too, but by then the
    // shell has already begun taking the scene apart.
    //
    // THROUGH THE VR API, which ends a PREVIEW through EditorVrPreview: that
    // object installed two callbacks on the editor viewport and redirected its
    // fly keys, and ending the session underneath it left all of that in place
    // — harmless at shutdown only for as long as the order never changes,
    // which is not a thing to rely on. The plain path below is the fallback for
    // a session this process's API object cannot reach any more.
    if (api && api->endForShutdown()) return;
    if (!host.engine) return;
    const auto e = host.engine->engine();
    if (!e) return;
    if (e->vrStatus().active) {
        if (host.engine->driver()) host.engine->driver()->setVrSessionActive(false);
        e->setVrMirrorView(nullptr);
        e->endVrSession();
    }
}

bool VrModule::toggleEditorPreview()
{
    if (!api) return false;
    if (api->editorPreviewActive()) { api->end(); return false; }
    return api->begin();
}

bool VrModule::isEditorPreviewActive() const
{
    return api && api->editorPreviewActive();
}
