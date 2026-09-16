/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/importsettingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

#include "ui/style/themeroles.h"

namespace
{

/// The six signed axis names, in the order the combos list them. The record
/// speaks these strings (iris::ImportSettings::axisFromName).
const char *const kAxisNames[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

/// The units combo's rows, in order. "auto" first: the file's own word is the
/// default answer, and the row says what that word was.
struct UnitRow { const char *key; const char *label; };
const UnitRow kUnitRows[] = {
    { "auto", QT_TRANSLATE_NOOP("ImportSettingsDialog", "Auto (what the file says)") },
    { "m",    QT_TRANSLATE_NOOP("ImportSettingsDialog", "Metres") },
    { "cm",   QT_TRANSLATE_NOOP("ImportSettingsDialog", "Centimetres") },
    { "mm",   QT_TRANSLATE_NOOP("ImportSettingsDialog", "Millimetres") },
    { "in",   QT_TRANSLATE_NOOP("ImportSettingsDialog", "Inches") },
    { "ft",   QT_TRANSLATE_NOOP("ImportSettingsDialog", "Feet") },
};

QString formatMetres(double v)
{
    return QString::number(v, 'g', 3);
}

iris::Vec3 axisVector(const QString &name)
{
    iris::Vec3 v(0, 1, 0);
    iris::ImportSettings::axisFromName(name, &v);
    return v;
}

}   // namespace

// ---------------------------------------------------------------------------
// THE ARITHMETIC — pure, and the reason ui.import_dialog needs no widgets
// ---------------------------------------------------------------------------

double ImportSettingsDialog::unitInForce(const iris::ImportSettings &settings,
                                         const iris::ModelPreRead &facts)
{
    // `units: "auto"` means "the file's declaration is right"; any other value
    // OVERRIDES it (irisgl/import/importsettings.h). A file that declares
    // nothing declares one metre per unit.
    const double override_ = iris::ImportSettings::unitFactor(settings.units);
    if (override_ > 0.0) return override_;
    return facts.declaredUnitScale > 0.0 ? facts.declaredUnitScale : 1.0;
}

ImportSettingsDialog::Box ImportSettingsDialog::placedBox(const iris::ImportSettings &settings,
                                                          const iris::ModelPreRead &facts)
{
    Box out;
    if (!facts.parsed || !facts.aabbValid) return out;

    // metres = source units x the unit in force x the user's scale — the exact
    // composition assimp's GLOBAL_SCALE_FACTOR performs at the parse
    // (ImportTransform::globalScaleFactor composed with the file's own scale).
    const double k = settings.scale * unitInForce(settings, facts);

    // The axes fix and the free rotation as ONE rotation, from the record
    // itself, so the preview can never disagree with the import about what
    // "+Z up" means. The eight corners are carried through it and re-boxed:
    // exact for the axis fixes (a signed permutation) and the honest
    // conservative answer for a free rotation.
    const iris::Quat rotation = settings.transform(facts.declaredUnitScale).rotation;

    bool first = true;
    for (int c = 0; c < 8; ++c) {
        const iris::Vec3 corner(float(((c & 1) ? facts.aabbMax[0] : facts.aabbMin[0]) * k),
                                float(((c & 2) ? facts.aabbMax[1] : facts.aabbMin[1]) * k),
                                float(((c & 4) ? facts.aabbMax[2] : facts.aabbMin[2]) * k));
        const iris::Vec3 p = rotation.rotatedVector(corner);
        const double v[3] = { p.x(), p.y(), p.z() };
        for (int a = 0; a < 3; ++a) {
            if (first) { out.min[a] = out.max[a] = v[a]; continue; }
            out.min[a] = std::min(out.min[a], v[a]);
            out.max[a] = std::max(out.max[a], v[a]);
        }
        first = false;
    }
    out.valid = true;
    return out;
}

void ImportSettingsDialog::originTranslation(Origin origin, const Box &box, double out[3])
{
    out[0] = out[1] = out[2] = 0.0;
    if (!box.valid || origin == Origin::Keep || origin == Origin::Custom) return;
    out[0] = -box.centre(0);
    out[2] = -box.centre(2);
    out[1] = origin == Origin::Centre ? -box.centre(1) : -box.min[1];
}

ImportSettingsDialog::Origin ImportSettingsDialog::originOf(const iris::ImportSettings &settings,
                                                            const Box &box)
{
    const double t[3] = { settings.translate[0], settings.translate[1], settings.translate[2] };
    if (t[0] == 0.0 && t[1] == 0.0 && t[2] == 0.0) return Origin::Keep;
    // A helper's answer is a computed double; a record that came back from
    // JSON carries the same double. Compare with a tolerance scaled to the
    // model, so a millimetre-sized asset and a kilometre-sized one are both
    // recognised.
    const double scale = box.valid
                             ? std::max({ std::fabs(box.size(0)), std::fabs(box.size(1)),
                                          std::fabs(box.size(2)), 1e-6 })
                             : 1.0;
    const double tol = 1e-6 * scale;
    for (Origin candidate : { Origin::Centre, Origin::BottomCentre }) {
        double want[3];
        originTranslation(candidate, box, want);
        if (std::fabs(want[0] - t[0]) <= tol && std::fabs(want[1] - t[1]) <= tol
            && std::fabs(want[2] - t[2]) <= tol)
            return candidate;
    }
    return Origin::Custom;
}

ImportSettingsDialog::Suggestion
ImportSettingsDialog::suggestionFor(const iris::ImportSettings &settings,
                                    const iris::ModelPreRead &facts, QString *text)
{
    if (text) text->clear();
    // ADVICE ABOUT A CHARACTER, and only that: a crate can be any size at all,
    // so there is nothing honest to say about one. A rigged file is the one
    // case where the world tells us what the answer should look like.
    if (!facts.parsed || !facts.rigged()) return Suggestion::None;
    const Box box = placedBox(settings, facts);
    if (!box.valid) return Suggestion::None;
    const double height = box.size(1);
    if (!(height > 0.0)) return Suggestion::None;

    if (height >= kMinCharacterHeight && height <= kMaxCharacterHeight) {
        if (text)
            *text = QCoreApplication::translate("ImportSettingsDialog",
                                                "%1 m tall — a plausible character.")
                        .arg(formatMetres(height));
        return Suggestion::Plausible;
    }

    // WHICH unit would land it in the envelope? The answer is a sentence, not
    // an action: the user may well have meant a giant.
    QString cure;
    const double wanted = unitInForce(settings, facts) * (kTypicalCharacterHeight / height);
    for (const UnitRow &row : kUnitRows) {
        iris::ImportSettings::Units units;
        if (!iris::ImportSettings::unitFromName(QLatin1String(row.key), &units)) continue;
        const double factor = iris::ImportSettings::unitFactor(units);
        if (factor <= 0.0) continue;                      // "auto" has no number
        if (std::fabs(factor - wanted) <= 0.1 * wanted) {
            cure = QCoreApplication::translate("ImportSettingsDialog",
                                               " A wrong unit? Try %1.")
                       .arg(QCoreApplication::translate("ImportSettingsDialog", row.label));
            break;
        }
    }
    if (text)
        *text = QCoreApplication::translate("ImportSettingsDialog",
                                            "%1 m tall — unusual for a character.")
                    .arg(formatMetres(height))
                + cure;
    return height > kMaxCharacterHeight ? Suggestion::TooLarge : Suggestion::TooSmall;
}

// ---------------------------------------------------------------------------
// THE WIDGET
// ---------------------------------------------------------------------------

ImportSettingsDialog::ImportSettingsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("importSettingsDialog"));
    buildUi();
    writeSettingsIntoFields();
    refreshPreview();
}

ImportSettingsDialog::~ImportSettingsDialog() = default;

void ImportSettingsDialog::buildUi()
{
    setWindowTitle(tr("Import settings"));
    setMinimumWidth(460);

    auto *outer = new QVBoxLayout(this);
    outer->setSpacing(10);

    mHeader = new QLabel(this);
    ThemeRoles::setTextSize(mHeader, 15, QFont::DemiBold);
    mHeader->setWordWrap(true);
    outer->addWidget(mHeader);

    mFileFacts = new QLabel(this);
    mFileFacts->setWordWrap(true);
    ThemeRoles::setTone(mFileFacts, ThemeRoles::Tone::Muted);
    outer->addWidget(mFileFacts);

    // ---- SIZE ------------------------------------------------------------
    auto *sizeBox = new QGroupBox(tr("Size"), this);
    auto *sizeForm = new QFormLayout(sizeBox);

    mUnits = new QComboBox(sizeBox);
    for (const UnitRow &row : kUnitRows)
        mUnits->addItem(tr(row.label), QLatin1String(row.key));
    mUnits->setToolTip(tr("What one unit in the file is worth. \"Auto\" trusts the file's own "
                          "declaration; anything else overrides it."));
    sizeForm->addRow(tr("Units"), mUnits);

    mScale = new QDoubleSpinBox(sizeBox);
    mScale->setDecimals(4);
    mScale->setRange(0.0001, 100000.0);
    mScale->setSingleStep(0.1);
    mScale->setValue(1.0);
    mScale->setToolTip(tr("A uniform scale on top of the unit. Baked in — every placement of "
                          "this model is at scale 1."));
    sizeForm->addRow(tr("Scale"), mScale);
    outer->addWidget(sizeBox);

    // ---- ORIENTATION -----------------------------------------------------
    auto *axisBox = new QGroupBox(tr("Orientation"), this);
    auto *axisForm = new QFormLayout(axisBox);

    mUp = new QComboBox(axisBox);
    mForward = new QComboBox(axisBox);
    for (const char *name : kAxisNames) {
        mUp->addItem(QLatin1String(name), QLatin1String(name));
        mForward->addItem(QLatin1String(name), QLatin1String(name));
    }
    mUp->setToolTip(tr("Which axis points UP in the file. Jahshaka's world is +Y up, -Z forward."));
    mForward->setToolTip(tr("Which axis points FORWARD in the file."));
    auto *axisRow = new QWidget(axisBox);
    auto *axisRowLayout = new QHBoxLayout(axisRow);
    axisRowLayout->setContentsMargins(0, 0, 0, 0);
    axisRowLayout->addWidget(new QLabel(tr("Up"), axisRow));
    axisRowLayout->addWidget(mUp, 1);
    axisRowLayout->addSpacing(8);
    axisRowLayout->addWidget(new QLabel(tr("Forward"), axisRow));
    axisRowLayout->addWidget(mForward, 1);
    axisForm->addRow(tr("File axes"), axisRow);

    auto *rotRow = new QWidget(axisBox);
    auto *rotLayout = new QHBoxLayout(rotRow);
    rotLayout->setContentsMargins(0, 0, 0, 0);
    static const char *const kRotLabels[] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; ++i) {
        rotLayout->addWidget(new QLabel(QLatin1String(kRotLabels[i]), rotRow));
        mRotate[i] = new QDoubleSpinBox(rotRow);
        mRotate[i]->setDecimals(2);
        mRotate[i]->setRange(-360.0, 360.0);
        mRotate[i]->setSingleStep(15.0);
        mRotate[i]->setSuffix(QStringLiteral("°"));
        rotLayout->addWidget(mRotate[i], 1);
    }
    axisForm->addRow(tr("Rotation"), rotRow);
    outer->addWidget(axisBox);

    // ---- ORIGIN ----------------------------------------------------------
    auto *originBox = new QGroupBox(tr("Origin"), this);
    auto *originForm = new QFormLayout(originBox);

    mOrigin = new QComboBox(originBox);
    mOrigin->addItem(tr("Keep the file's origin"), int(Origin::Keep));
    mOrigin->addItem(tr("Centre of the model"), int(Origin::Centre));
    mOrigin->addItem(tr("Bottom centre (stands on the ground)"), int(Origin::BottomCentre));
    mOrigin->addItem(tr("Custom"), int(Origin::Custom));
    originForm->addRow(tr("Place at"), mOrigin);

    auto *transRow = new QWidget(originBox);
    auto *transLayout = new QHBoxLayout(transRow);
    transLayout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        transLayout->addWidget(new QLabel(QLatin1String(kRotLabels[i]), transRow));
        mTranslate[i] = new QDoubleSpinBox(transRow);
        mTranslate[i]->setDecimals(4);
        mTranslate[i]->setRange(-100000.0, 100000.0);
        mTranslate[i]->setSingleStep(0.1);
        mTranslate[i]->setSuffix(tr(" m"));
        transLayout->addWidget(mTranslate[i], 1);
    }
    originForm->addRow(tr("Offset"), transRow);
    outer->addWidget(originBox);

    // ---- WHAT TO IMPORT --------------------------------------------------
    auto *tuneBox = new QGroupBox(tr("What to import"), this);
    auto *tuneLayout = new QVBoxLayout(tuneBox);

    mSkeleton = new QCheckBox(tr("Skeleton (a rigged file imports as static geometry without it)"),
                              tuneBox);
    mSkeleton->setChecked(true);
    tuneLayout->addWidget(mSkeleton);

    mMaterials = new QCheckBox(tr("Materials and textures"), tuneBox);
    mMaterials->setChecked(true);
    tuneLayout->addWidget(mMaterials);

    auto *clipRow = new QWidget(tuneBox);
    auto *clipRowLayout = new QHBoxLayout(clipRow);
    clipRowLayout->setContentsMargins(0, 0, 0, 0);
    clipRowLayout->addWidget(new QLabel(tr("Animation clips"), clipRow));
    mClipMode = new QComboBox(clipRow);
    mClipMode->addItem(tr("All"));
    mClipMode->addItem(tr("None"));
    mClipMode->addItem(tr("Choose…"));
    clipRowLayout->addWidget(mClipMode, 1);
    tuneLayout->addWidget(clipRow);

    mClipList = new QListWidget(tuneBox);
    mClipList->setMaximumHeight(120);
    mClipList->setVisible(false);
    ThemeRoles::setFrame(mClipList, QFrame::NoFrame);
    tuneLayout->addWidget(mClipList);
    outer->addWidget(tuneBox);

    // ---- WHAT IT WILL MEASURE -------------------------------------------
    mExtent = new QLabel(this);
    ThemeRoles::setTextSize(mExtent, 13, QFont::DemiBold);
    outer->addWidget(mExtent);

    mSuggestion = new QLabel(this);
    mSuggestion->setWordWrap(true);
    mSuggestion->setVisible(false);
    outer->addWidget(mSuggestion);

    mStatus = new QLabel(this);
    mStatus->setWordWrap(true);
    mStatus->setVisible(false);
    outer->addWidget(mStatus);

    mRemainingBox = new QCheckBox(this);
    mRemainingBox->setVisible(false);
    outer->addWidget(mRemainingBox);

    mButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mOkButton = mButtons->button(QDialogButtonBox::Ok);
    outer->addWidget(mButtons);
    connect(mButtons, &QDialogButtonBox::accepted, this, &ImportSettingsDialog::accept);
    connect(mButtons, &QDialogButtonBox::rejected, this, &ImportSettingsDialog::reject);

    // ---- the field -> record -> preview loop -----------------------------
    const auto changed = [this]() {
        if (mUpdating) return;
        readFieldsIntoSettings();
        refreshPreview();
    };
    connect(mUnits, &QComboBox::currentIndexChanged, this, changed);
    connect(mScale, &QDoubleSpinBox::valueChanged, this, changed);
    connect(mUp, &QComboBox::currentIndexChanged, this, [this, changed]() {
        if (mUpdating) return;
        keepAxesPerpendicular(true);
        changed();
    });
    connect(mForward, &QComboBox::currentIndexChanged, this, [this, changed]() {
        if (mUpdating) return;
        keepAxesPerpendicular(false);
        changed();
    });
    for (int i = 0; i < 3; ++i) {
        connect(mRotate[i], &QDoubleSpinBox::valueChanged, this, [this, changed]() {
            if (mUpdating) return;
            // A rotation moves the box, so a helper's offset has to follow it.
            changed();
            applyOriginHelper();
        });
        // A typed offset is a CUSTOM origin — the combo says so rather than
        // lying about which helper produced it.
        connect(mTranslate[i], &QDoubleSpinBox::valueChanged, this, [this, changed]() {
            if (mUpdating) return;
            changed();
            const Origin now = originOf(mSettings, placedBox(mSettings, mFacts));
            mUpdating = true;
            mOrigin->setCurrentIndex(mOrigin->findData(int(now)));
            mUpdating = false;
        });
    }
    connect(mOrigin, &QComboBox::currentIndexChanged, this, [this]() {
        if (mUpdating) return;
        applyOriginHelper();
    });
    connect(mClipMode, &QComboBox::currentIndexChanged, this, [this, changed]() {
        if (mUpdating) return;
        mClipList->setVisible(mClipMode->currentIndex() == 2);
        changed();
    });
    connect(mClipList, &QListWidget::itemChanged, this, changed);
    connect(mSkeleton, &QCheckBox::toggled, this, changed);
    connect(mMaterials, &QCheckBox::toggled, this, changed);

    setMode(mMode);
}

void ImportSettingsDialog::setMode(Mode mode)
{
    mMode = mode;
    if (!mOkButton) return;
    mOkButton->setText(mode == Mode::Reimport ? tr("Reimport") : tr("Import"));
    setWindowTitle(mode == Mode::Reimport ? tr("Import settings") : tr("Import model"));
    if (auto *cancel = mButtons->button(QDialogButtonBox::Cancel))
        cancel->setText(mMode == Mode::Import && mRemaining > 0 ? tr("Skip this file")
                                                                : tr("Cancel"));
    setSubject(mSubject);
}

void ImportSettingsDialog::setSubject(const QString &displayName)
{
    mSubject = displayName;
    if (!mHeader) return;
    if (mSubject.isEmpty()) {
        mHeader->setText(mMode == Mode::Reimport ? tr("Import settings")
                                                 : tr("Import settings"));
        return;
    }
    mHeader->setText(mMode == Mode::Reimport
                         ? tr("Reimport “%1”").arg(mSubject)
                         : tr("Import “%1”").arg(mSubject));
}

void ImportSettingsDialog::setRemaining(int n)
{
    mRemaining = std::max(0, n);
    if (!mRemainingBox) return;
    mRemainingBox->setVisible(mRemaining > 0 && mMode == Mode::Import);
    mRemainingBox->setText(mRemaining == 1
                               ? tr("Use these settings for the other model file too")
                               : tr("Use these settings for the remaining %1 model files")
                                     .arg(mRemaining));
    if (mRemaining <= 0) mRemainingBox->setChecked(false);
    if (auto *cancel = mButtons->button(QDialogButtonBox::Cancel))
        cancel->setText(mMode == Mode::Import && mRemaining > 0 ? tr("Skip this file")
                                                                : tr("Cancel"));
}

bool ImportSettingsDialog::applyToRemaining() const
{
    return mRemainingBox && mRemainingBox->isVisible() && mRemainingBox->isChecked();
}

void ImportSettingsDialog::setSettings(const iris::ImportSettings &settings)
{
    mSettings = settings;
    writeSettingsIntoFields();
    refreshPreview();
}

iris::ImportSettings ImportSettingsDialog::settings() const
{
    return mSettings;
}

void ImportSettingsDialog::setCommitHandler(Commit commit)
{
    mCommit = std::move(commit);
}

void ImportSettingsDialog::setPreRead(const iris::ModelPreRead &facts)
{
    mFacts = facts;
    setBusy(false);
    refreshClipList();
    // THE BOX IS ONLY KNOWN NOW. A helper the user picked while the read was in
    // flight computed nothing (there was no box) — apply it for real; otherwise
    // name whichever helper the offset the record carries turns out to be.
    const Origin named = origin();
    if (named == Origin::Centre || named == Origin::BottomCentre) {
        applyOriginHelper();
    } else {
        mUpdating = true;
        mOrigin->setCurrentIndex(std::max(
            0, mOrigin->findData(int(originOf(mSettings, placedBox(mSettings, mFacts))))));
        mUpdating = false;
    }
    refreshPreview();
}

void ImportSettingsDialog::setBusy(bool busy)
{
    mBusy = busy;
    if (mOkButton) mOkButton->setEnabled(!busy);
    refreshPreview();
}

void ImportSettingsDialog::startPreRead(const QString &path, const QString &formatHint)
{
    const quint64 generation = ++mPreReadGeneration;
    setBusy(true);
    // A worker, because a large FBX is seconds even for the light parse — the
    // dialog stays alive and cancellable throughout. The watcher is a child of
    // this dialog, so a dialog closed mid-read simply stops listening; the
    // generation counter drops a superseded read's answer.
    auto *watcher = new QFutureWatcher<iris::ModelPreRead>(this);
    connect(watcher, &QFutureWatcher<iris::ModelPreRead>::finished, watcher,
            [this, watcher, generation]() {
                const iris::ModelPreRead facts = watcher->result();
                watcher->deleteLater();
                if (generation != mPreReadGeneration) return;   // superseded
                setPreRead(facts);
                if (!facts.parsed)
                    setStatus(tr("This file could not be read: %1").arg(facts.error), true);
            });
    watcher->setFuture(QtConcurrent::run(&iris::ModelPreRead::read, path, formatHint));
}

// ---------------------------------------------------------------------------

void ImportSettingsDialog::readFieldsIntoSettings()
{
    mSettings.scale = mScale->value();
    iris::ImportSettings::unitFromName(mUnits->currentData().toString(), &mSettings.units);
    mSettings.up = mUp->currentData().toString();
    mSettings.forward = mForward->currentData().toString();
    for (int i = 0; i < 3; ++i) {
        mSettings.rotate[i] = mRotate[i]->value();
        mSettings.translate[i] = mTranslate[i]->value();
    }
    mSettings.skeleton = mSkeleton->isChecked();
    mSettings.materials = mMaterials->isChecked() ? iris::ImportSettings::MaterialMode::Import
                                                  : iris::ImportSettings::MaterialMode::None;
    const bool keptClips = mSettings.clips;
    const QStringList keptNames = mSettings.clipNames;
    mSettings.clipNames.clear();
    switch (mClipMode->currentIndex()) {
    case 0: mSettings.clips = true; break;
    case 1: mSettings.clips = false; break;
    default:
        // A list with NO ROWS is "the pre-read has not landed yet", not "the
        // user unticked everything": reading it would erase the clip choice a
        // reimport came in with, silently, the moment any other field moved.
        if (mClipList->count() == 0) {
            mSettings.clips = keptClips;
            mSettings.clipNames = keptNames;
            break;
        }
        mSettings.clips = true;
        for (int row = 0; row < mClipList->count(); ++row) {
            const QListWidgetItem *item = mClipList->item(row);
            if (item->checkState() == Qt::Checked) mSettings.clipNames.append(item->text());
        }
        // An empty choice IS "no clips" — the record says so explicitly
        // rather than silently meaning "all of them".
        if (mSettings.clipNames.isEmpty()) mSettings.clips = false;
        break;
    }
}

void ImportSettingsDialog::writeSettingsIntoFields()
{
    if (!mScale) return;
    mUpdating = true;
    // A RECORD IS NOT A WIDGET'S OPINION: a scale the spin box cannot represent
    // exactly would be silently rounded the moment any other field moved. Widen
    // the box for the value instead — the common 1.0 still reads "1.0000".
    if (mSettings.scale > 0.0) {
        while (mScale->decimals() < 9
               && std::fabs(QString::number(mSettings.scale, 'f', mScale->decimals()).toDouble()
                            - mSettings.scale)
                      > 1e-12 * mSettings.scale)
            mScale->setDecimals(mScale->decimals() + 1);
        if (mSettings.scale < mScale->minimum()) mScale->setMinimum(mSettings.scale);
        if (mSettings.scale > mScale->maximum()) mScale->setMaximum(mSettings.scale);
    }
    mScale->setValue(mSettings.scale);
    mUnits->setCurrentIndex(
        std::max(0, mUnits->findData(QLatin1String(iris::ImportSettings::unitName(mSettings.units)))));
    mUp->setCurrentIndex(std::max(0, mUp->findData(mSettings.up)));
    mForward->setCurrentIndex(std::max(0, mForward->findData(mSettings.forward)));
    for (int i = 0; i < 3; ++i) {
        mRotate[i]->setValue(mSettings.rotate[i]);
        mTranslate[i]->setValue(mSettings.translate[i]);
    }
    mSkeleton->setChecked(mSettings.skeleton);
    mMaterials->setChecked(mSettings.materials == iris::ImportSettings::MaterialMode::Import);
    if (!mSettings.clipNames.isEmpty()) mClipMode->setCurrentIndex(2);
    else mClipMode->setCurrentIndex(mSettings.clips ? 0 : 1);
    mClipList->setVisible(mClipMode->currentIndex() == 2);
    mOrigin->setCurrentIndex(
        std::max(0, mOrigin->findData(int(originOf(mSettings, placedBox(mSettings, mFacts))))));
    mUpdating = false;
    refreshClipList();
}

void ImportSettingsDialog::refreshClipList()
{
    if (!mClipList) return;
    mUpdating = true;
    mClipList->clear();
    for (const QString &name : mFacts.clipNames) {
        auto *item = new QListWidgetItem(name, mClipList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool on = mSettings.clipNames.isEmpty()
                            ? mSettings.clips
                            : mSettings.wantsClip(name);
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    // A record naming clips this file does not carry is still the record: the
    // list shows what the FILE has, the record keeps what the user asked for.
    const bool haveClips = !mFacts.clipNames.isEmpty();
    mClipMode->setEnabled(haveClips || !mFacts.parsed);
    mUpdating = false;
}

void ImportSettingsDialog::keepAxesPerpendicular(bool upChanged)
{
    const iris::Vec3 up = axisVector(mUp->currentData().toString());
    const iris::Vec3 forward = axisVector(mForward->currentData().toString());
    if (std::fabs(double(iris::Vec3::dotProduct(up, forward))) <= 1e-4) return;

    // The record REFUSES a parallel pair (importsettings.cpp), so the dialog
    // cannot offer one: the axis the user did not just touch moves to the
    // first one perpendicular to the axis they did.
    QComboBox *fix = upChanged ? mForward : mUp;
    const iris::Vec3 fixed = upChanged ? up : forward;
    for (const char *name : kAxisNames) {
        const iris::Vec3 candidate = axisVector(QLatin1String(name));
        if (std::fabs(double(iris::Vec3::dotProduct(fixed, candidate))) > 1e-4) continue;
        mUpdating = true;
        fix->setCurrentIndex(fix->findData(QLatin1String(name)));
        mUpdating = false;
        return;
    }
}

void ImportSettingsDialog::applyOriginHelper()
{
    const Origin wanted = Origin(mOrigin->currentData().toInt());
    if (wanted == Origin::Custom) return;
    double t[3];
    originTranslation(wanted, placedBox(mSettings, mFacts), t);
    mUpdating = true;
    for (int i = 0; i < 3; ++i) mTranslate[i]->setValue(t[i]);
    mUpdating = false;
    readFieldsIntoSettings();
    refreshPreview();
}

ImportSettingsDialog::Origin ImportSettingsDialog::origin() const
{
    return mOrigin ? Origin(mOrigin->currentData().toInt()) : Origin::Keep;
}

void ImportSettingsDialog::setOrigin(Origin origin)
{
    if (!mOrigin) return;
    mUpdating = true;
    mOrigin->setCurrentIndex(std::max(0, mOrigin->findData(int(origin))));
    mUpdating = false;
    applyOriginHelper();
}

QString ImportSettingsDialog::extentText() const
{
    return mExtent ? mExtent->text() : QString();
}

ImportSettingsDialog::Suggestion ImportSettingsDialog::suggestion() const
{
    QString ignored;
    return suggestionFor(mSettings, mFacts, &ignored);
}

QString ImportSettingsDialog::suggestionText() const
{
    QString text;
    suggestionFor(mSettings, mFacts, &text);
    return text;
}

void ImportSettingsDialog::refreshPreview()
{
    if (!mExtent) return;

    if (mBusy) {
        mExtent->setText(tr("Reading the file…"));
        ThemeRoles::setTone(mExtent, ThemeRoles::Tone::Muted);
        mFileFacts->setText(QString());
        mSuggestion->setVisible(false);
        return;
    }

    const Box box = placedBox(mSettings, mFacts);
    if (!box.valid) {
        mExtent->setText(mFacts.parsed ? tr("Extent: nothing to measure")
                                       : tr("Extent: not measured yet"));
        ThemeRoles::setTone(mExtent, ThemeRoles::Tone::Muted);
    } else {
        mExtent->setText(tr("Extent: %1 × %2 × %3 m")
                             .arg(formatMetres(box.size(0)), formatMetres(box.size(1)),
                                  formatMetres(box.size(2))));
        ThemeRoles::setTone(mExtent, ThemeRoles::Tone::Normal);
    }

    if (mFacts.parsed) {
        QStringList facts;
        facts << tr("the file says 1 unit = %1 m").arg(formatMetres(mFacts.declaredUnitScale));
        const auto count = [](int n, const QString &one, const QString &many) {
            return n == 1 ? one.arg(n) : many.arg(n);
        };
        if (mFacts.rigged())
            facts << count(mFacts.bones, tr("%1 bone"), tr("%1 bones"));
        if (!mFacts.clipNames.isEmpty())
            facts << count(int(mFacts.clipNames.size()), tr("%1 clip"), tr("%1 clips"));
        if (mFacts.materials > 0)
            facts << count(mFacts.materials, tr("%1 material"), tr("%1 materials"));
        mFileFacts->setText(facts.join(tr(" · ")));
    }

    QString advice;
    const Suggestion state = suggestionFor(mSettings, mFacts, &advice);
    mSuggestion->setVisible(state != Suggestion::None);
    mSuggestion->setText(advice);
    ThemeRoles::setTone(mSuggestion, state == Suggestion::Plausible ? ThemeRoles::Tone::Success
                                                                    : ThemeRoles::Tone::Warning);
}

void ImportSettingsDialog::setStatus(const QString &text, bool problem)
{
    if (!mStatus) return;
    mStatus->setText(text);
    mStatus->setVisible(!text.isEmpty());
    ThemeRoles::setTone(mStatus, problem ? ThemeRoles::Tone::Error : ThemeRoles::Tone::Muted);
}

void ImportSettingsDialog::accept()
{
    readFieldsIntoSettings();

    // IMPORT MODE: the record is the answer — the caller puts it on the
    // ImportRequest. REIMPORT MODE: OK re-bakes, through the verb its opener
    // handed us (assets.reimport), so the open-scene swap and the bake-store
    // memo clear happen exactly as they do for a script.
    if (mMode == Mode::Reimport && mCommit) {
        setStatus(tr("Reimporting…"), false);
        if (mOkButton) mOkButton->setEnabled(false);
        QString error;
        const bool ok = mCommit(record(), &error);
        if (mOkButton) mOkButton->setEnabled(true);
        if (!ok) {
            setStatus(error.isEmpty() ? tr("The reimport failed.") : error, true);
            return;   // stay open: the settings are still the user's to fix
        }
    }
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// THE BATCH
// ---------------------------------------------------------------------------

QHash<QString, QJsonObject> ImportSettingsDialog::askForFiles(const QStringList &modelFiles,
                                                              QWidget *parent)
{
    QHash<QString, QJsonObject> out;
    ImportSettingsBatch batch;
    batch.setFiles(modelFiles);
    while (!batch.atEnd()) {
        if (!batch.needsPrompt()) {
            batch.takeSticky();
            continue;
        }
        const QString file = batch.current();
        ImportSettingsDialog dialog(parent);
        dialog.setMode(Mode::Import);
        dialog.setSubject(QFileInfo(file).fileName());
        dialog.setRemaining(batch.remaining());
        dialog.startPreRead(file);
        if (dialog.exec() == QDialog::Accepted)
            batch.accept(dialog.record(), dialog.applyToRemaining());
        else
            batch.skip();
    }
    for (const QString &file : batch.accepted()) out.insert(file, batch.recordFor(file));
    return out;
}

void ImportSettingsBatch::setFiles(const QStringList &modelFiles)
{
    mFiles = modelFiles;
    mIndex = 0;
    mSticky = false;
    mStickyRecord = QJsonObject();
    mRecords.clear();
    mAccepted.clear();
    mSkipped.clear();
}

void ImportSettingsBatch::accept(const QJsonObject &record, bool useForRemaining)
{
    if (atEnd()) return;
    const QString file = mFiles.at(mIndex);
    mRecords.insert(file, record);
    mAccepted.append(file);
    if (useForRemaining) {
        mSticky = true;
        mStickyRecord = record;
    }
    ++mIndex;
}

void ImportSettingsBatch::takeSticky()
{
    if (atEnd() || !mSticky) return;
    const QString file = mFiles.at(mIndex);
    mRecords.insert(file, mStickyRecord);
    mAccepted.append(file);
    ++mIndex;
}

void ImportSettingsBatch::skip()
{
    if (atEnd()) return;
    // THIS FILE ONLY (§8): the rest of the drop is still wanted. A "use for
    // the remaining" already in force stays in force — the user turned the
    // question off, they did not answer it for this file.
    mSkipped.append(mFiles.at(mIndex));
    ++mIndex;
}
