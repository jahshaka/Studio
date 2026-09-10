/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLIPBOARDBACKEND_H
#define CLIPBOARDBACKEND_H

// Where the payload lives (CLIPBOARD_SPEC §5).
//
// SystemClipboardBackend is the product: QClipboard, the payload offered under
// BOTH `application/x-jahshaka-clipboard` and `text/plain` (D2 c) — the custom
// type so our own paste never sniffs and other applications ignore us, the
// text so the copy crosses to a text editor, a chat window, another machine's
// clipboard and back.
//
// MemoryBackend is the same contract with no QClipboard behind it: the codec,
// the closure and the resolver are then testable in a process with no window
// system at all, and a headless session cannot be surprised by a platform
// clipboard that does not exist.

#include <QByteArray>

class ClipboardBackend
{
public:
    virtual ~ClipboardBackend() = default;

    /// Publishes the payload under both MIME types. An empty payload clears.
    virtual void setPayload(const QByteArray &payload) = 0;

    /// The payload, preferring the custom type; falls back to `text/plain`
    /// (which is what a payload that travelled through a text editor arrives
    /// as). Empty when the clipboard holds something else.
    virtual QByteArray payload() const = 0;
};

/// In-process, no window system.
class MemoryClipboardBackend : public ClipboardBackend
{
public:
    void setPayload(const QByteArray &payload) override { mPayload = payload; }
    QByteArray payload() const override { return mPayload; }

private:
    QByteArray mPayload;
};

/// QClipboard, both MIME types.
class SystemClipboardBackend : public ClipboardBackend
{
public:
    void setPayload(const QByteArray &payload) override;
    QByteArray payload() const override;

    /// True when this process has a platform clipboard at all.
    static bool available();
};

#endif // CLIPBOARDBACKEND_H
