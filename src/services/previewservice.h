/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SERVICES_PREVIEWSERVICE_H
#define SERVICES_PREVIEWSERVICE_H

// THE PREVIEW SERVICE (PREVIEWS-1): every stored preview of a library row — a material on the
// studio sphere, a model framed to its bounds, an image's own pixels, a pose strip, a candela lobe,
// an icon — is made HERE and stored by ONE writer (services/previewwriter.h), with the key of what
// it depicts (services/previewkey.h). It replaces thumbrebuild (the per-type switch and the sweep),
// thumbnailstop (the sweep's stop), assetthumb (the model render) and materialtile (the mint door).
//
// QObject-free; the ENGINE is the shell's, handed in as a provider (the one adapter that knows
// where it lives is the shell's construction), so a suite runs this with none: the CPU kinds work
// headless and the engine kinds fail by name.
//
// A RENDER IS A REQUEST WITH A COMPLETION: request(project, guid, done). The KEY is taken AT THE
// REQUEST and written WITH the bytes, so a definition that moves in flight reads stale and is drawn
// again. The render is inline today (PICTURE-REQUEST-1 stage 3 makes it a ticket on Ready); ONE
// render is in flight at a time — the one thumbnail renderer's borrow refuses a second by name. A
// failed or cancelled delivery CHANGES NOTHING: no image, no write. An empty material definition is
// a named failure. `done` is called at most once — at once when nothing is written, else on the
// application's thread when the writer has landed (or failed, or superseded) the write - and NOT at
// all for a write still queued when the service is destroyed (its completion dies with the writer,
// the write itself drained). THIS SERVICE NEVER WAITS
// AND NEVER SPINS AN EVENT LOOP: the one wait for a landed write lives in the synchronous verbs'
// door (scripting/modules/previewsapi.cpp), bounded, and PICTURE-REQUEST-1 stage 3 replaces it by a
// ticket. INSIDE A SCRIPT RUN the run's database batch holds the library's write lock from its first
// write (Database::beginBatch), so a preview requested there waits in the writer and LANDS at the
// batch's next stand-down (any top-level DbTransaction, the previews verbs' own) or at the run's end;
// until then its row reads missing or stale.
//
// AN IMPORT's PICTURE is made on its prepare worker and stored through store() after the import's
// commit (Database::setImportPictureSink, set by this service's owner): the key read then, the bytes
// and the key to the one writer — the import's transaction writes no tile.
//
// THE STATE of every row is a comparison, never a render: none (no preset, or no definition: nothing
// to draw), missing (no bytes), stale (bytes whose stored key is not the key now — none stored is
// stale too), ok. One catalog statement a call (Database::fetchPreviewRows).

#include <QHash>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

class Database;
class Project;

namespace jahshaka { namespace engine { class Engine; } }
namespace previews { class Writer; }

class PreviewService
{
public:
    using EngineProvider = std::function<std::shared_ptr<jahshaka::engine::Engine>()>;

    /// `db` null or closed: no writer, and every request fails by name.
    PreviewService(Database *db, EngineProvider engine);
    ~PreviewService();
    PreviewService(const PreviewService &) = delete;
    PreviewService &operator=(const PreviewService &) = delete;

    struct Outcome
    {
        bool ok = false;
        QString reason;               ///< empty when ok
        /// The row has nothing to draw and never had (a builtin primitive's Object row, a type with
        /// no preset): a failure to the caller who asked for THIS guid, a skip to a sweep.
        bool nothingToDraw = false;
        /// A newer request of the same row replaced this one while it waited in the writer: not a
        /// failure (the newer one is the row's preview).
        bool superseded = false;
    };
    using Done = std::function<void(const Outcome &)>;

    /// THE ONE REQUEST. `size` empty: the preset's own tile size.
    void request(Project *project, const QString &guid, Done done, QSize size = QSize());

    /// THE STORE DOOR: a picture made ELSEWHERE (an import's, on its prepare worker) stored through
    /// the one writer with the row's key read NOW — the second half of request(), no render. The
    /// same refusals as request (no such row, no preset, nothing to draw, a null picture) answer
    /// at once and store nothing.
    void store(Project *project, const QString &guid, const QImage &picture, Done done);

    /// THE MINT DOOR (a material just created, copied or saved): a request whose failure is
    /// logged naming `who` when a render COULD have happened — headless, "no picture" is the
    /// expected answer and is not said. Never waits. `then` (optional) hears the outcome after that.
    void mint(Project *project, const QString &guid, const char *who, Done then = {});

    enum class State { None, Missing, Stale, Ok };
    static const char *stateName(State state);
    struct Row
    {
        QString guid;
        int type = 0;
        State state = State::None;
        QString key;         ///< the key now; empty for none
        QString storedKey;   ///< the key the stored bytes were drawn from; empty when none was
    };
    /// Every library row (projectOnly: the open project's pinned rows), `type` >= 0 one type; a
    /// non-empty `guids` those rows only (the render demand's read: by guid, never the library).
    QVector<Row> state(Project *project, bool projectOnly, int type = -1, const QStringList &guids = QStringList());
    /// "N previews to refresh": state()'s rows TALLIED - the same one catalog read (the key is a hash
    /// the catalog cannot compute, so the comparison is this side's), no per-row statement.
    struct Counts
    {
        int ok = 0;
        int missing = 0;
        int stale = 0;
        int none = 0;
    };
    Counts counts(Project *project, bool projectOnly);

    /// THE EMPTY STUDIO of a studio-lit preset's view (the material view, or the asset view of an
    /// Object, ParticleSystem, Avatar and Mesh) at `size`: the control a tile's SUBJECT coverage is
    /// measured against (previews.coverage). Rendered ONCE a view, size and previewenv version and
    /// kept; `fresh` renders it again and keeps nothing (the control's own control). Null with
    /// `reason` for a CPU preset (no studio) or no engine.
    QImage emptyStudio(int type, QSize size, bool fresh, QString *reason);

    // ---- THE SWEEP (renderAll) -------------------------------------------------------------
    struct SweepOptions
    {
        bool staleOnly = true;     ///< missing or stale rows only; false redraws every drawable row
        bool projectOnly = false;  ///< the open project's pinned rows
        int limit = 0;             ///< 0 = no limit, else at most this many requests
        std::function<bool()> cancelled;   ///< an extra stop source, asked once a row
        /// THE DOOR'S WAIT AT THE WRITER'S CAP (previews::Writer::kQueueCap): called while that many
        /// writes are pending; it waits (bounded, the door's own loop) and answers whether one LANDED.
        /// Null, or false: the sweep stops there, `stalled`. This service never waits itself.
        std::function<bool()> waitForLanding;
    };
    struct SweepResult
    {
        int considered = 0;        ///< drawable rows looked at
        int requested = 0;         ///< requests made; each completes through `each`
        int skipped = 0;           ///< nothing to draw
        bool cancelled = false;    ///< a stop arrived before the end
        bool stalled = false;      ///< the writer stayed at its cap: the sweep stopped there
        int peakPending = 0;       ///< the most writes pending in the writer during the sweep
    };
    /// One request a row, `yield` between rows (the caller keeps its UI alive with it; a library
    /// can hold hundreds of renders), `each` with every row's outcome as it completes.
    SweepResult sweep(Project *project, const SweepOptions &options,
                      const std::function<void(const QString &guid, const Outcome &)> &each,
                      const std::function<void()> &yield = {});

    /// THE STOP: a sweep yields between rows, and a yield can deliver the window's close or a
    /// script's stop — the sweep stops at its next row. STICKY until a new intent clears it (a
    /// script run starting, the Assets page's own menu entry).
    void requestStop() { mStop = true; }
    void clearStop() { mStop = false; }
    bool stopRequested() const { return mStop; }
    /// A sweep is in flight (a counter: a yield can start another): what `app.resetLibrary`
    /// refuses on (services/libraryreset.h).
    bool sweepRunning() const { return mSweeps > 0; }

    // ---- THE WAITING DOORS' VIEW OF THE WRITER (scripting/modules/previewsapi.cpp) -------------
    /// Writes submitted to the one writer and not yet completed, whoever asked (any thread).
    int pendingWrites() const;
    /// THE LANDING NOTICE: called on the application's thread after EVERY write the writer
    /// completes — landed, failed or superseded, a request's or a store's. ONE listener; the call
    /// answers the previous one, which a nested wait puts back when it ends. This service never
    /// waits on it.
    using LandingFn = std::function<void()>;
    LandingFn setLandingListener(LandingFn listener);

private:
    QImage renderPicture(Project *project, const QString &guid, int type, QSize size, Outcome *out);
    /// The checks a stored picture needs, then the key: false with `refused` filled when the row
    /// cannot take one (request's and store's shared first half).
    bool keyFor(Project *project, const QString &guid, int *typeOut, QString *keyOut, Outcome *refused);
    /// The picture and its key to the one writer, `done` at its completion (the shared second half).
    void submit(const QString &guid, const QImage &picture, const QString &key, Done done);

    Database *mDb = nullptr;
    EngineProvider mEngine;
    std::unique_ptr<previews::Writer> mWriter;
    std::atomic<bool> mStop{ false };   // ScriptEngine::stop is called off the UI thread
    int mSweeps = 0;
    /// The landing listener, shared with every completion in flight (never `this` across the writer).
    std::shared_ptr<LandingFn> mLanding = std::make_shared<LandingFn>();
    QHash<QString, QImage> mEmptyStudio;   ///< emptyStudio's kept pictures, by view|width|height|previewenv version
};

#endif   // SERVICES_PREVIEWSERVICE_H
