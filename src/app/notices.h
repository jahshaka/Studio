/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NOTICES_H
#define NOTICES_H

// notices — THE THIRD-PARTY NOTICES THIS BINARY OWES (NOTICES-1, 2026-09-18).
//
// The app had no in-app notice surface for ANY vendored dependency — found by
// the VR-INPUT-1E read while checking the controller meshes' MIT licence
// (ledger §666 item 10: "the licence is complete for the repo; the app has no
// third-party notice surface"). Ten components ship inside this executable and
// a user could not see one of their licences.
//
// ONE DECLARATION, TWO CONSUMERS, AND NO COPIED TEXT. `app/notices.json` is the
// manifest — id, name, what it does for us, homepage, licence name, and WHERE
// its notice lives in the vendored tree. cmake/Notices.cmake turns that into
// resources at configure time: the licence file itself where there is one, an
// extracted line range where the notice lives in a comment (QtAwesome's header,
// the WebXR assets' provenance). So the text in the binary IS the text in the
// tree — nothing here is retyped, and a vendored licence that changes changes
// the binary's copy on the next build.
//
// THE COVERAGE RULE IS A TEST, not a habit: `source.notices_coverage` walks
// `thirdparty/` and `irisgl/thirdparty/` and fails on a vendored directory the
// manifest does not claim, so the next `git submodule add` cannot ship
// unacknowledged.
//
// API-FIRST: `app.notices()` is the verb (the list; `{id}` adds the text) and
// the About dialog's "Third-party notices" page calls exactly that data.

#include <QString>
#include <QVector>

namespace notices {

/// One vendored component, as the manifest declares it. `text` is NOT here —
/// it is read on demand (a licence is kilobytes and the list is a list).
struct Entry
{
    QString id;         ///< stable, script-facing ("assimp", "ogre-next")
    QString name;       ///< human ("Open Asset Import Library (assimp)")
    QString role;       ///< what it does for Jahshaka, one line
    QString homepage;
    QString licence;    ///< the short name, for the list column
    QString path;       ///< the vendored directory, repo-relative
    QString file;       ///< the notice file inside it (the manifest's own record)
    /// Does THIS BUILD carry the component? Computed at configure time
    /// (cmake/Notices.cmake) from whether its notice file was there and, for
    /// breakpad, from DISABLE_BREAKPAD — never from the manifest's shape.
    bool present = true;
    /// Is the component's source IN THIS TREE? False for Qt (linked
    /// dynamically) and for the Vulkan loader and MoltenVK (redistributed
    /// inside the macOS bundle): their licence texts live in app/notices/ with
    /// their provenance, which is the one exception to reading a notice off the
    /// code it covers.
    bool vendored = true;
};

/// The manifest, in its declared order. Parsed once from the embedded resource;
/// empty only if the resource is missing, which is a build defect and is why
/// the coverage test exists.
const QVector<Entry> &entries();

/// One component's notice TEXT, read from the embedded resource. Empty for an
/// unknown id.
QString text(const QString &id);

}   // namespace notices

#endif   // NOTICES_H
