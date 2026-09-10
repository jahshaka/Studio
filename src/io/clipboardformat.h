/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLIPBOARDFORMAT_H
#define CLIPBOARDFORMAT_H

// THE CLIPBOARD ENVELOPE — one versioned TEXT payload for every domain
// (SPECS/CLIPBOARD_SPEC.md §2, decisions D1(c) / D2(c) / D4).
//
// Unreal puts T3D on the clipboard: the same text a .t3d file holds, so a copy
// survives a text editor, a chat window, a second instance and a different
// build. This is that idea with our document model's shape, and one thing
// Unreal's cannot do — a reference here can carry its BYTES, because every
// asset in this app is content-addressed.
//
//   {"format":"jahshaka.clipboard","version":1,"app":"0.9.1b","sceneFormat":2,
//    "source":{"storeId":"…","projectGuid":"…","storeRoot":"/abs/path"},
//    "created":"2026-09-10T10:00:00Z",
//    "items":[{"kind":"node","node":{…},"parent":"…","index":3}],
//    "assets":{"<guid>":{"name":"brick.png","type":"texture","typeId":4,
//                        "dependencies":[],
//                        "files":[{"role":"source","name":"brick.png",
//                                  "ext":"png","size":8123,"oid":"<sha256>",
//                                  "inline":"<base64>"}]}}}
//
// COMPACT JSON, ONE LINE, UTF-8. No binary framing and no compression: the
// payload has to be legible as text (that is the whole cross-instance,
// cross-version story), and a compressed blob is not self-identifying in a
// text field. Size is bounded by the inline threshold instead (§2.3).
//
// TOLERANCE IS THE CONTRACT, in both directions:
//   * an item `kind` this build does not know is SKIPPED and reported, never a
//     refusal of the whole envelope (the same rule sceneformat.h's unknown
//     node type has);
//   * `version` guards the ENVELOPE only. `sceneFormat` says which node-object
//     version the `node` items were written in, so they route through exactly
//     the tolerant SceneReader path a file of that version does;
//   * `app` is informational — shown in a report, never a gate.
//
// This file is a LEAF: QJson plus QString. No database, no services, no
// document — the codec is unit-testable on its own (tests/services/
// test_clipboard.cpp) and the service above it does the rest.

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace clipboardformat {

/// The `format` key's value — the self-identifying marker a reader sniffs for
/// before it parses (Unreal's `Begin Map`).
inline const char *kFormatId() { return "jahshaka.clipboard"; }

/// The envelope version this build writes.
constexpr int kVersion = 1;

/// THE CEILING on a payload, in bytes. Not a policy about how much a user may
/// copy — the inline budget (`clipboard/inlineLimitBytes`, 4 MB) is that — but
/// a bound on what this process will PARSE or PUBLISH. A clipboard is a shared
/// resource any application can fill: on X11 the selection owner serves the
/// bytes, clipboard MANAGERS archive every `text/plain` they see (and sync them
/// to phones), and a 500 MB text selection from another app must cost a size
/// check here rather than a base64 decode of half a gigabyte.
constexpr qint64 kMaxPayloadBytes = 64ll * 1024 * 1024;

/// How much of a payload the cheap sniff looks at. Generous on purpose: the
/// marker leads the payloads WE write, but a payload that went through a
/// pretty-printer or any tool that re-sorted the keys puts `assets` first, and
/// refusing those would break the one property this format exists for.
constexpr int kSniffBytes = 8192;

/// The custom MIME type the payload is offered under beside `text/plain`
/// (D2 c): our own paste reads it without sniffing, other applications ignore
/// it, and clipboard MANAGERS do not archive it.
inline const char *kMimeType() { return "application/x-jahshaka-clipboard"; }

/// The item kinds this build knows. A kind outside this list is carried
/// through a read (so `clipboard.text()` still round-trips it) and skipped by
/// a paste with a reason.
namespace kind {
inline const char *node()     { return "node"; }
inline const char *material() { return "material"; }
inline const char *graph()    { return "graph"; }
inline const char *anim()     { return "anim"; }
inline const char *asset()    { return "asset"; }
} // namespace kind

/// One file of one asset in the closure.
struct ClipFile
{
    QString role;             // "source" | "texture" | "bake" | …
    QString name;             // display / staging file name
    QString ext;              // lowercase, no dot (the CAS object's extension)
    qint64 size = -1;
    QString oid;              // sha256 hex of the bytes; the join key everywhere
    QByteArray inlineData;    // the bytes themselves, when they fit the budget
    bool hasBytes() const { return !inlineData.isEmpty(); }
};

/// One asset the items reference — the manifest v2 asset entry
/// (src/export/exportmanifest.h) plus the optional inline bytes.
struct ClipAsset
{
    QString guid;
    QString name;
    QString type;             // the assets.* vocabulary ("texture", "object", …)
    int typeId = -1;          // raw ModelTypes value (lossless)
    QStringList dependencies; // outgoing edges, guids
    /// The catalog row's own placement: `parent` is the guidchain a member row
    /// hangs under (an imported model's Texture rows hang under the Object),
    /// `viewFilter` is AssetViewFilter — a library TILE (2) or a member the
    /// library does not list on its own (1). A resolver that registered every
    /// row at the root with the default filter would turn one pasted model
    /// into a library full of loose textures.
    QString parent;
    int viewFilter = -1;
    QVector<ClipFile> files;
    QByteArray blob;          // the row's `asset` column (Object/Material rows)
    QByteArray properties;    // the row's `properties` column
};

/// One copied thing.
struct ClipItem
{
    QString kind;
    QJsonObject data;         // the kind's payload, verbatim

    /// `node` items: the fragment shape (`node`, `parent`, `index`) minus the
    /// session `nodeIds`, which mean nothing outside the process that made them.
    QJsonObject nodeObject() const { return data.value(QStringLiteral("node")).toObject(); }
    QString parentGuid() const { return data.value(QStringLiteral("parent")).toString(); }
    int siblingIndex() const { return data.value(QStringLiteral("index")).toInt(-1); }
    /// A short human name for reports ("Cube", "brick.png", "3 graph nodes").
    QString displayName() const;
};

/// Where a payload came from. `storeId` is the LIBRARY's identity
/// (assetstore.h) — "same library" is decided by id, never by path;
/// `storeRoot` is a same-machine hint the resolver may read from (§3.4 step 4).
struct ClipSource
{
    QString storeId;
    QString projectGuid;
    QString storeRoot;
};

struct Envelope
{
    int version = kVersion;
    QString app;              // Constants::CONTENT_VERSION of the writer
    int sceneFormat = 0;      // sceneformat::kVersion of the node items
    ClipSource source;
    QString created;          // ISO-8601 UTC
    QVector<ClipItem> items;
    QMap<QString, ClipAsset> assets;   // guid -> closure entry

    bool isNull() const { return items.isEmpty(); }
    /// Items of one kind, in order.
    QVector<ClipItem> itemsOfKind(const QString &kind) const;
    /// Total inline bytes carried (decoded, not base64).
    qint64 inlineBytes() const;

    /// Compact single-line JSON, UTF-8.
    QByteArray toText() const;

    /// Parses a payload. `error` receives WHY on failure — a caller reports it
    /// (clipboard.setText) rather than throwing, because "this text is not a
    /// Jahshaka payload" is an ordinary answer, not a fault.
    static Envelope fromText(const QByteArray &text, QString *error = nullptr);

    /// Cheap sniff: does this text START like an envelope? Used before a parse
    /// so a clipboard holding a novel or 30 MB of base64 from another app is
    /// never JSON-parsed. Unreal's CanImportNodesFromText, same job.
    static bool looksLikeEnvelope(const QByteArray &text);
};

} // namespace clipboardformat

#endif // CLIPBOARDFORMAT_H
