/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// previews.key — THE PREVIEW KEY MOVES WITH WHAT THE PICTURE DEPICTS, AND WITH NOTHING ELSE
// (PREVIEWS-1 stage 1, spec call 2; services/previewkey.h). The pure function only: no catalog.
//
//   1. it MOVES on each input alone: a definition oid, a member oid, the project pin, the preset's
//      version (here: a row of another preset with the same content), previewenv's version;
//   2. it is EQUAL under any order of the two oid lists (no name is an input: the RENAME law is the
//      catalog's, driven by assets.rename in scripting.e2e.previews_state);
//   3. previewenv's version moves a MATERIAL's key and NOT a TEXTURE's (a texture's tile is its
//      own pixels: the studio is not in it — the lead's ruling A3);
//   4. a row with no content of its own (no file, no pin) keys on its GUID: two such rows differ,
//      the same row is equal; a row WITH content does not key on its guid (a copy keeps its key);
//   5. a type with no preset (Mesh, Sky, a SoundEffect today) has no key.

#include <algorithm>
#include <cstdio>


#include "data/project.h"
#include "services/previewkey.h"

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

previews::KeySource material()
{
    previews::KeySource s;
    s.guid = QStringLiteral("aaaaaaaa-0000-0000-0000-000000000001");
    s.type = int(ModelTypes::Material);
    s.files = { QStringLiteral("source:1111"), QStringLiteral("bake:2222") };
    s.members = { QStringLiteral("tex-a:3333"), QStringLiteral("tex-b:4444") };
    return s;
}


}   // namespace

int main()
{
    const int env = 1;
    const previews::KeySource base = material();
    const QString k = previews::key(base, env);
    check(k.size() == 16, "the key is 16 hex digits");
    bool hex = true;
    for (const QChar c : k) hex = hex && (c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')));
    check(hex, "the key is lowercase hex");
    check(previews::key(base, env) == k, "the key is a function of its inputs (equal twice)");

    // 1. Each input alone moves it.
    previews::KeySource s = base;
    s.files[0] = QStringLiteral("source:1112");
    check(previews::key(s, env) != k, "a definition oid moves the key");
    s = base;
    s.members[1] = QStringLiteral("tex-b:4445");
    check(previews::key(s, env) != k, "a member oid moves the key");
    s = base;
    s.members.append(QStringLiteral("tex-c:5555"));
    check(previews::key(s, env) != k, "a new member moves the key");
    s = base;
    s.pin = QStringLiteral("9999");
    check(previews::key(s, env) != k, "the open project's pin moves the key");
    previews::KeySource other = base;
    other.pin = QStringLiteral("8888");
    check(previews::key(s, env) != previews::key(other, env), "two different pins, two keys");
    s = base;
    s.blob = QStringLiteral("0123456789abcdef");
    check(previews::key(s, env) != k, "the blob's stamp (an in-place edit of the row's blob) moves the key");
    check(previews::key(base, env + 1) != k, "previewenv's version moves a material's key");
    s = base;
    s.type = int(ModelTypes::Object);   // the ASSET preset over the same content
    check(previews::key(s, env) != k, "the preset is in the key (same content, another preset)");

    // 2. Order.
    s = base;
    std::reverse(s.files.begin(), s.files.end());
    std::reverse(s.members.begin(), s.members.end());
    check(previews::key(s, env) == k, "the oid lists' order is not in the key");

    // 3. The studio is not in a texture's key.
    previews::KeySource tex;
    tex.guid = QStringLiteral("tttttttt-0000-0000-0000-000000000001");
    tex.type = int(ModelTypes::Texture);
    tex.files = { QStringLiteral("source:abcd") };
    check(!previews::key(tex, env).isEmpty(), "a texture has a key");
    check(previews::key(tex, env) == previews::key(tex, env + 1),
          "previewenv's version does NOT move a texture's key");
    previews::KeySource texMoved = tex;
    texMoved.files = { QStringLiteral("source:abce") };
    check(previews::key(texMoved, env) != previews::key(tex, env), "a texture's oid moves its key");

    // 4. The guid, only for a row with no content of its own.
    previews::KeySource bare;
    bare.guid = QStringLiteral("oooooooo-0000-0000-0000-000000000001");
    bare.type = int(ModelTypes::Object);
    previews::KeySource bare2 = bare;
    bare2.guid = QStringLiteral("oooooooo-0000-0000-0000-000000000002");
    check(previews::key(bare, env) != previews::key(bare2, env), "two content-less rows key on their guids");
    check(previews::key(bare, env) == previews::key(bare, env), "the same content-less row keys alike");
    previews::KeySource copy = base;
    copy.guid = QStringLiteral("aaaaaaaa-0000-0000-0000-000000000099");
    check(previews::key(copy, env) == k, "a row WITH content does not key on its guid (a copy keeps its key)");
    previews::KeySource bareMember = bare;
    bareMember.members = { QStringLiteral("mesh-1:7777") };
    check(previews::key(bareMember, env) != previews::key(bare, env),
          "a content-less Object's MESH member moves its key");

    // 5. No preset, no key.
    for (const ModelTypes t : { ModelTypes::Mesh, ModelTypes::Sky, ModelTypes::SoundEffect, ModelTypes::Undefined }) {
        previews::KeySource n = base;
        n.type = int(t);
        check(previews::key(n, env).isEmpty() && !previews::presetFor(int(t)),
              "a type with no preset has no key");
    }

    std::printf("previews.key: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
