/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIVEVIDEO_H
#define LIVEVIDEO_H

// VIDEO INTO A LIVE TEXTURE (ASSET_MEDIA_SPEC §3 Option A, the shape
// MATERIAL_GAPS_SPEC A-1 names): a decoded frame becomes the pixels of a live
// texture, and the ordinary live-texture machinery does the rest — the
// generation moves, SceneMirror uploads once, the material samples it.
//
// QVideoSink, exactly like the Assets page's preview (src/ui/controls/
// videopreviewwidget.cpp) and the thumbnail grabber (services/videoutils.cpp):
// Qt Multimedia's ffmpeg backend decodes and hands us RGBA on the CPU, which
// is verified headless-safe and needs no shader-side YUV path. Option B
// (libav, GPU planes) is the scale answer and is not this.
//
// GUI THREAD ONLY, like every other Qt Multimedia object in this tree. The
// scripting host runs there, which is where the verbs call from.

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QMediaPlayer;
class QVideoSink;
class QVideoFrame;
QT_END_NAMESPACE

class LiveVideoBinding : public QObject
{
    Q_OBJECT
public:
    LiveVideoBinding(const QString &videoGuid, const QString &textureGuid,
                     const QString &filePath, QObject *parent = nullptr);

    bool play();
    bool pause();
    bool stop();
    bool seek(qint64 positionMs);
    void setLoop(bool loop);
    bool loop() const { return looping; }

    /// Decodes and delivers the NEXT frame, synchronously: spins a local event
    /// loop until one lands in the texture or `timeoutMs` expires, leaving the
    /// player in the state it found it in. The primitive a test, a batch tool
    /// or a thumbnail sweep needs — everything else about video is
    /// asynchronous by nature, and asserting on "some frames probably arrived"
    /// is how a suite becomes a flake.
    bool step(int timeoutMs = 4000);

    QVariantMap state() const;
    QString textureGuid() const { return liveGuid; }
    quint64 frames() const { return frameCount; }

signals:
    void frameDelivered();

private:
    void onFrame(const QVideoFrame &frame);

    QString videoGuid;
    QString liveGuid;
    QString source;
    QMediaPlayer *player = nullptr;
    QVideoSink   *sink = nullptr;
    bool     looping = false;
    quint64  frameCount = 0;
    QString  lastError;
};

class LiveVideo
{
public:
    /// Binds a decoded video to a live texture. One binding per VIDEO guid;
    /// re-binding the same video replaces it. Returns null and fills `error`
    /// when the file cannot be opened or the texture does not exist.
    static LiveVideoBinding *bind(const QString &videoGuid, const QString &textureGuid,
                                  const QString &filePath, QString *error = nullptr);
    static bool unbind(const QString &videoGuid);
    static LiveVideoBinding *find(const QString &videoGuid);
    static QStringList bound();
    /// Drops every binding (the app's shutdown path; a stray QMediaPlayer
    /// outliving the media backend is a crash at exit).
    static void clear();
};

#endif // LIVEVIDEO_H
