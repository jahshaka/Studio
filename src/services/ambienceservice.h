/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AMBIENCESERVICE_H
#define AMBIENCESERVICE_H

// THE WORLD'S AMBIENT MUSIC, PLAYED (audit D8).
//
// The document says WHAT plays — iris::Scene::ambientMusicGuid (a Music asset)
// and ambientMusicVolume (1..100) — and nothing else. This service is the one
// thing that turns that into sound: a QMediaPlayer with a SOURCE and a
// QAudioOutput, the two halves the document's old player never had (it was
// built with neither, so every world with music played silence).
//
// WHY A SERVICE AND NOT THE DOCUMENT. iris::Scene is data: it is built by
// every process that reads a scene (the importer, the bakers, the preview
// scenes, the headless suites), and a media player inside it put the Qt
// multimedia backend on all of those paths and leaked one player per scene.
// Playback is a property of the RUNNING EDITOR, like the engine mirror is: the
// service reads the document and mirrors it to the audio device, the way
// SceneMirror mirrors it to the GPU. Every writer — the World panel, the verb,
// undo, a scene open — only writes the two fields; sync() is the one reader.
//
// sync() is called once per render tick (the shell wires it to the driver's
// beforeFrame, which fires every tick even while nothing is drawn) and by the
// verb, so a script sees the result at once. It costs a string compare and a
// float compare when nothing changed, and builds no player until a world with
// music asks for one.
//
// GUI THREAD ONLY, like every Qt Multimedia object in this tree.

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

#include "irisgl/irisglfwd.h"

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QMediaPlayer;
QT_END_NAMESPACE

class AmbienceService : public QObject
{
    Q_OBJECT
public:
    /// `resolve` maps a Music asset guid to the file it plays (the project's
    /// pin, else the library's bytes); empty when there is none.
    using Resolver = std::function<QString(const QString &guid)>;
    explicit AmbienceService(Resolver resolve, QObject *parent = nullptr);
    ~AmbienceService() override;

    /// Mirrors `scene`'s music to the player: a new guid gets a new source and
    /// starts (looping), the same guid keeps playing where it is, a changed
    /// volume is applied in place, and no scene or no guid stops it.
    void sync(const iris::ScenePtr &scene);
    /// Stops and forgets the source (a world closed).
    void stop();
    /// The file a Music guid resolves to (empty when none) — the same
    /// resolution sync() plays from; the web export copies it.
    QString fileFor(const QString &guid) const;

    /// {guid, source, state: "playing" | "stopped" | "paused", output (the
    ///  player has its audio output), gain (the output's linear volume),
    ///  error} — what world.ambience reports.
    QVariantMap state() const;

private:
    void ensurePlayer();

    Resolver mResolve;
    QMediaPlayer *mPlayer = nullptr;
    QAudioOutput *mOutput = nullptr;
    QString mGuid;        ///< the guid the player's source was resolved from
    QString mSource;      ///< the file that guid resolved to
    float mVolume = -1.0f;
    QString mError;       ///< why the last guid could not play, empty when it did
};

#endif // AMBIENCESERVICE_H
