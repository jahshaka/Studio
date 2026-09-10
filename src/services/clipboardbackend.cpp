/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/clipboardbackend.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include "io/clipboardformat.h"

bool SystemClipboardBackend::available()
{
    return QGuiApplication::instance() != nullptr && QGuiApplication::clipboard() != nullptr;
}

void SystemClipboardBackend::setPayload(const QByteArray &payload)
{
    QClipboard *clipboard = available() ? QGuiApplication::clipboard() : nullptr;
    if (!clipboard) return;
    if (payload.isEmpty()) { clipboard->clear(); return; }
    // The ceiling applies to what we PUBLISH too: a clipboard manager archives
    // every text/plain it sees, and handing one tens of megabytes stalls it and
    // syncs it to the user's phone. Refusing leaves the previous payload alone,
    // which is the same contract an empty copy has.
    if (qint64(payload.size()) > clipboardformat::kMaxPayloadBytes) return;

    // ONE QMimeData carrying the SAME bytes twice. Ownership passes to Qt.
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(clipboardformat::kMimeType()), payload);
    mime->setText(QString::fromUtf8(payload));
    clipboard->setMimeData(mime);
}

QByteArray SystemClipboardBackend::payload() const
{
    QClipboard *clipboard = available() ? QGuiApplication::clipboard() : nullptr;
    if (!clipboard) return QByteArray();
    const QMimeData *mime = clipboard->mimeData();
    if (!mime) return QByteArray();

    const QString custom = QString::fromLatin1(clipboardformat::kMimeType());
    if (mime->hasFormat(custom)) {
        const QByteArray data = mime->data(custom);
        if (qint64(data.size()) > clipboardformat::kMaxPayloadBytes) return QByteArray();
        return data;
    }
    if (!mime->hasText()) return QByteArray();

    // text/plain: only if it IS one of ours. A clipboard holding a paragraph
    // of prose must cost a prefix test, never a JSON parse (§6.2).
    const QByteArray text = mime->text().toUtf8();
    if (qint64(text.size()) > clipboardformat::kMaxPayloadBytes) return QByteArray();
    if (!clipboardformat::Envelope::looksLikeEnvelope(text)) return QByteArray();
    return text;
}
