/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef IMPORTSETTINGSDIALOG_H
#define IMPORTSETTINGSDIALOG_H

// THE IMPORT DIALOG (SPECS/IMPORT_DIALOG_SPEC.md §8) — where a person decides,
// ONCE, how big a model is, which way up it stands and where its origin sits.
//
// THE DECISION BEHIND IT (owner + lead, MASTER_QUEUE §527/§528): an asset's
// scale, orientation and origin are BAKED INTO THE ASSET at import, so every
// placement of it is at scale 1. Nothing in the editor rescales an instance any
// more — the old fit-to-size envelope and the avatar height rule are both gone.
// This dialog is the only place that decision is made, and it opens on EVERY
// model import: a drop, the Assets page's browse, the Avatar module's import.
// Media never prompts.
//
// WHAT IT PRODUCES is an iris::ImportSettings record (irisgl/import/
// importsettings.h) — the very object `assets.import(path, {...})` parses from
// a script — so a dialog import and a verb import are byte-identical records
// and key the same bake. The dialog NEVER writes to the library itself: in
// import mode the record rides on the ImportRequest, and in reimport mode OK
// calls the `assets.reimport` verb through the commit handler its opener wires.
// (API-first, SCRIPTING_SPEC §2.3: the verbs landed in lane 1, with tests,
// before this dialog existed.)
//
// WHAT IT KNOWS comes from a LIGHT PRE-READ on a worker thread
// (iris::ModelPreRead — a no-post-processing parse, the ~3x cheaper half):
// the file's own unit declaration, its bounding box in SOURCE UNITS, whether
// it is rigged, and the names its clips would import as. The box stays in
// source units on purpose — the whole point of the units field is to let a
// person disagree with the file, and the live "Extent" preview is
//
//     metres = source units x (the unit in force) x (the user's scale)
//
// rotated by the axes fix and the free rotation. Every one of those steps is a
// static function below, so ui.import_dialog can drive the arithmetic without
// a widget in sight.
//
// THEME: Qlementine through ThemeRoles only — no raw stylesheet anywhere
// (theme.no_raw_sheets), and the dialog is registered in the app's dialog
// catalog (shell/mainwindowdialogs.cpp) so the live theme walk opens it twice
// like every other dialog.

#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QJsonObject>
#include <QString>
#include <functional>

#include "irisgl/import/importsettings.h"
#include "irisgl/import/modelsceneinfo.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
template <typename T> class QFutureWatcher;

class ImportSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /// IMPORT: the file is not in the library yet and the record rides on the
    /// ImportRequest. REIMPORT: the asset exists, the fields come pre-filled
    /// from `assets.importSettings` and OK re-bakes it through
    /// `assets.reimport`.
    enum class Mode { Import, Reimport };

    /// The origin helpers. They are PURE UI: each one computes a `translate`
    /// from the pre-read's box, and the record only ever carries the metres.
    enum class Origin {
        Keep,          ///< the file's own origin — translate (0,0,0)
        Centre,        ///< the box's centre lands on the origin
        BottomCentre,  ///< centred in X/Z, standing ON the ground plane
        Custom         ///< a translation the user typed
    };

    /// The suggestion line's states. It is ADVICE ABOUT A CHARACTER and never
    /// applies itself (the retired auto-fit did, and guessed wrong).
    enum class Suggestion {
        None,       ///< not a rigged file, or nothing measurable
        Plausible,  ///< a human-sized character
        TooLarge,
        TooSmall
    };

    explicit ImportSettingsDialog(QWidget *parent = nullptr);
    ~ImportSettingsDialog() override;

    void setMode(Mode mode);
    Mode mode() const { return mMode; }

    /// The file or asset the dialog is about (display name only).
    void setSubject(const QString &displayName);
    QString subject() const { return mSubject; }

    /// THE BATCH (§8): `n` is how many MODEL files come after this one. Above
    /// zero the dialog offers "Use these for the remaining N" and its Cancel
    /// says it skips this file only.
    void setRemaining(int n);
    int remaining() const { return mRemaining; }
    bool applyToRemaining() const;

    /// Pre-fill / read back. `settings()` is what the dialog holds right now,
    /// whether or not it has been accepted.
    void setSettings(const iris::ImportSettings &settings);
    iris::ImportSettings settings() const;
    QJsonObject record() const { return settings().toJson(); }

    /// The facts. Setting them re-computes the preview, the clip checklist and
    /// the suggestion; `setBusy(true)` says a pre-read is in flight.
    void setPreRead(const iris::ModelPreRead &facts);
    const iris::ModelPreRead &preRead() const { return mFacts; }
    void setBusy(bool busy);
    bool isBusy() const { return mBusy; }

    /// Read `path` on a worker and feed the result to setPreRead() when it
    /// lands. Safe to call again (the earlier read's answer is dropped) and
    /// safe to destroy the dialog while one is in flight.
    ///
    /// `formatHint` (an extension, no dot) is needed only in reimport mode,
    /// where the bytes come straight from the content-addressed store and the
    /// object's name is its hash (irisgl/import/scenesource.h).
    void startPreRead(const QString &path, const QString &formatHint = QString());

    /// REIMPORT MODE: what OK does. The dialog holds no database and no
    /// services — its opener hands it the verb. Returning false with `*error`
    /// set keeps the dialog open with the message shown.
    using Commit = std::function<bool(const QJsonObject &record, QString *error)>;
    void setCommitHandler(Commit commit);

    /// THE IMPORT DECISION FOR A WHOLE DROP (§8): ask once per MODEL file,
    /// with "use these for the remaining N", and answer with the record each
    /// accepted file is to be imported with. A file the user SKIPPED is absent
    /// from the map — its drop-mates still import.
    ///
    /// A static, not a shell round-trip, because the import half needs nothing
    /// but widgets: the record it produces rides on the ImportRequest. (The
    /// REIMPORT half does need the assets.reimport verb, and lives in the
    /// shell — MainWindow::openImportSettings.)
    static QHash<QString, QJsonObject> askForFiles(const QStringList &modelFiles,
                                                   QWidget *parent);

    /// The suggestion line as it currently reads (empty for Suggestion::None).
    QString suggestionText() const;
    Suggestion suggestion() const;
    /// The live extent line, e.g. "Extent: 0.5 × 1.75 × 0.3 m".
    QString extentText() const;
    /// The origin helper the translate fields currently correspond to.
    Origin origin() const;
    void setOrigin(Origin origin);

    // ---- THE ARITHMETIC (pure; ui.import_dialog drives it directly) -------

    /// A box in metres.
    struct Box
    {
        double min[3] = { 0.0, 0.0, 0.0 };
        double max[3] = { 0.0, 0.0, 0.0 };
        bool valid = false;
        double size(int axis) const { return max[axis] - min[axis]; }
        double centre(int axis) const { return 0.5 * (min[axis] + max[axis]); }
    };

    /// Metres per source unit actually in force: the user's unit when they
    /// overrode one, else what the file declared.
    static double unitInForce(const iris::ImportSettings &settings,
                              const iris::ModelPreRead &facts);

    /// The pre-read's box carried into METRES and through the axes fix and the
    /// free rotation — the box the origin helpers measure and the preview
    /// prints. The record's `translate` is NOT applied: a translation moves the
    /// box, it does not resize it, and the helpers compute the translation FROM
    /// this box.
    static Box placedBox(const iris::ImportSettings &settings,
                         const iris::ModelPreRead &facts);

    /// The `translate` an origin helper asks for, given that box.
    static void originTranslation(Origin origin, const Box &box, double out[3]);

    /// Which helper `settings.translate` corresponds to (Custom when it is
    /// none of them).
    static Origin originOf(const iris::ImportSettings &settings, const Box &box);

    /// The advice line for a rigged file. `text` is filled for every state but
    /// None. It never changes the record.
    static Suggestion suggestionFor(const iris::ImportSettings &settings,
                                    const iris::ModelPreRead &facts, QString *text);

    /// A human character is between these, in metres. The envelope the retired
    /// auto-fit used to ENFORCE; here it only ever produces a sentence.
    static constexpr double kMinCharacterHeight = 0.5;
    static constexpr double kMaxCharacterHeight = 3.0;
    static constexpr double kTypicalCharacterHeight = 1.75;

public slots:
    void accept() override;

private:
    void buildUi();
    void readFieldsIntoSettings();
    void writeSettingsIntoFields();
    void refreshPreview();
    void refreshClipList();
    void keepAxesPerpendicular(bool upChanged);
    void applyOriginHelper();
    void setStatus(const QString &text, bool problem);

    Mode mMode = Mode::Import;
    QString mSubject;
    int mRemaining = 0;
    bool mBusy = false;
    bool mUpdating = false;           ///< guards the field <-> record round trip
    iris::ImportSettings mSettings;
    iris::ModelPreRead mFacts;
    Commit mCommit;
    quint64 mPreReadGeneration = 0;

    QLabel *mHeader = nullptr;
    QLabel *mFileFacts = nullptr;
    QComboBox *mUnits = nullptr;
    QDoubleSpinBox *mScale = nullptr;
    QComboBox *mUp = nullptr;
    QComboBox *mForward = nullptr;
    QDoubleSpinBox *mRotate[3] = { nullptr, nullptr, nullptr };
    QComboBox *mOrigin = nullptr;
    QDoubleSpinBox *mTranslate[3] = { nullptr, nullptr, nullptr };
    QCheckBox *mSkeleton = nullptr;
    QComboBox *mClipMode = nullptr;
    QListWidget *mClipList = nullptr;
    QCheckBox *mMaterials = nullptr;
    QLabel *mExtent = nullptr;
    QLabel *mSuggestion = nullptr;
    QLabel *mStatus = nullptr;
    QCheckBox *mRemainingBox = nullptr;
    QDialogButtonBox *mButtons = nullptr;
    QPushButton *mOkButton = nullptr;
    QFutureWatcher<iris::ModelPreRead> *mWatcher = nullptr;
};

/// THE BATCH (SPECS/IMPORT_DIALOG_SPEC.md §8, owner pick §12.5): a drop of
/// several files asks ONCE PER MODEL FILE, and the dialog offers "use these
/// for the remaining N". Cancel skips THAT FILE ONLY — the rest of the drop
/// still imports, which is why a batch needs a state machine rather than a
/// single answer.
///
/// No widgets: the pages own the prompting loop
///
///     while (!batch.atEnd()) {
///         if (batch.needsPrompt()) { ...show the dialog...
///             accepted ? batch.accept(record, useForRest) : batch.skip(); }
///         else batch.takeSticky();
///     }
///
/// and ui.import_dialog drives the same class with no dialog at all.
class ImportSettingsBatch
{
public:
    /// The MODEL files of a drop, in order. Media never prompts and never
    /// belongs here.
    void setFiles(const QStringList &modelFiles);

    bool atEnd() const { return mIndex >= mFiles.size(); }
    QString current() const { return atEnd() ? QString() : mFiles.at(mIndex); }
    /// Model files still to come AFTER the current one.
    int remaining() const { return atEnd() ? 0 : int(mFiles.size()) - mIndex - 1; }

    /// False once a dialog said "use these for the remaining N": the answer is
    /// already known and asking again would be the very thing the box turned
    /// off.
    bool needsPrompt() const { return !atEnd() && !mSticky; }
    /// Apply the sticky record to the current file and advance.
    void takeSticky();

    void accept(const QJsonObject &record, bool useForRemaining);
    void skip();

    QJsonObject recordFor(const QString &file) const { return mRecords.value(file); }
    QStringList accepted() const { return mAccepted; }
    QStringList skipped() const { return mSkipped; }

private:
    QStringList mFiles;
    int mIndex = 0;
    bool mSticky = false;
    QJsonObject mStickyRecord;
    QHash<QString, QJsonObject> mRecords;
    QStringList mAccepted;
    QStringList mSkipped;
};

#endif // IMPORTSETTINGSDIALOG_H
