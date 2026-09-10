/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for the panel suites that now carry the UNDO spine (debt L6).
//
// Every properties row is undoable, so the panel slices link
// SetNodePropertyCommand — which, for the three transform keys, restores a
// node's SCENE_STATIC classification through structuralundo. That one path
// reaches SceneEditService::rebuildFragment, i.e. the whole document-fragment
// stack, for a case no widget suite can produce (a panel row is never a
// reparent). Stubbed here so the suites stay widgets + a document, exactly as
// the other stubs in this directory do.

#include "data/database/database.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "services/sceneeditservice.h"

// A LIBRARY THE SUITE CAN HAND TO THE PANEL HOST.
//
// Every panel suite here passed `setDatabase(nullptr)` — which is exactly why
// the gate was blind to the defect this stub exists for: the properties panel
// forwarded the library to ONE of its children, so the sky rows, the emitter's
// image row and the shader panel were dead (or dereferenced the null) in the
// real app while every suite stayed green. The class's methods are already
// stubbed in this directory; these two make an INSTANCE possible, so a suite
// can prove the forwarding rather than assume it. No Sql connection is opened —
// nothing in this shell touches `db`.
Database::Database() = default;
Database::~Database() = default;

iris::SceneNodePtr SceneEditService::rebuildFragment(const SceneFragment &) const
{
    return iris::SceneNodePtr();
}

/// The emitter's image row binds through the SERVICE now (it used to write the
/// library rows by hand). The binding itself — project membership, the
/// dependency row, the CAS resolve — is out of a widget suite's reach; what the
/// suite can assert is that the row goes through this door and records a step,
/// so the stub answers "bound" and marks the node the way the real one would.
bool SceneEditService::setParticleTexture(const iris::ParticleSystemNodePtr &emitter,
                                          const QString &textureGuid)
{
    if (!emitter) return false;
    if (textureGuid.isEmpty()) { emitter->texture.clear(); return true; }
    emitter->setTexture(iris::Texture2D::createLive(textureGuid, textureGuid, 4, 4, false));
    return true;
}
