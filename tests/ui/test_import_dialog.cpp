/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.import_dialog — THE IMPORT DECISION, AS A DIALOG
// (SPECS/IMPORT_DIALOG_SPEC.md §8/§9).
//
// The dialog's ONE job is to produce an iris::ImportSettings record: the very
// object `assets.import(path, {...})` parses from a script. So the assertion
// that matters most is BYTE EQUALITY — the record the widgets emit must be the
// same canonical JSON (and therefore the same bake key) as the record a verb
// call with the same values produces. A dialog that produced an equivalent but
// differently-spelled record would silently double every bake in the library.
//
// Everything else here is the arithmetic the dialog SHOWS, driven through the
// static helpers so no widget needs to be on screen for it:
//   * the live extent preview = the pre-read's box in SOURCE UNITS x the unit
//     in force x the user's scale, carried through the axes fix;
//   * the origin helpers (Keep / Centre / Bottom-centre) as pure arithmetic on
//     that box;
//   * the suggestion line's states — it is advice about a CHARACTER and it
//     never applies itself;
//   * the batch state machine (one dialog per model file, "use these for the
//     remaining N", Cancel skips that file only).
//
// The pre-read itself is exercised on the real fixtures, because the whole
// preview rests on one fact that is easy to get backwards: a flags-0 parse
// runs NO aiProcess_GlobalScale, so its vertices are in the file's own units
// and the declaration is a separate number.
//
// Offscreen QPA, no database, no engine, no display.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QPushButton>
#include <cmath>
#include <cstdio>

#include "irisgl/import/importsettings.h"
#include "irisgl/import/modelsceneinfo.h"
#include "ui/dialogs/importsettingsdialog.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } \
} while (0)

static bool near_(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol; }

// Reach the dialog's widgets by object name-free lookup: the fields are
// private, and a test that pokes them by index would pin the LAYOUT rather
// than the behaviour. Everything below drives the public surface.
template <typename T>
static T *nth(QWidget *root, int index)
{
    const auto all = root->findChildren<T *>();
    return index < all.size() ? all.at(index) : nullptr;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString cubeCm = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR
                                          "/tests/importer/fixtures/unit_cube_cm.fbx");
    const QString cubeM = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR
                                         "/tests/importer/fixtures/unit_cube_m.fbx");
    const QString rig = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR
                                       "/tests/avatar/fixtures/rig2.glb");

    // ---- 1. THE PRE-READ: source units, and the declaration beside them ----
    const iris::ModelPreRead cm = iris::ModelPreRead::read(cubeCm);
    CHECK(cm.parsed, "the pre-read parses unit_cube_cm.fbx");
    CHECK(near_(cm.declaredUnitScale, 0.01, 1e-9),
          "…and reports what the FILE declares: 1 unit = 0.01 m");
    CHECK(cm.aabbValid, "…with a measurable box");
    const double rawY = cm.aabbMax[1] - cm.aabbMin[1];
    std::printf("    raw box: %g x %g x %g (source units)\n",
                cm.aabbMax[0] - cm.aabbMin[0], rawY, cm.aabbMax[2] - cm.aabbMin[2]);
    CHECK(near_(rawY, 1.0, 1e-3),
          "…IN SOURCE UNITS: a 1-unit cube reads 1, not 0.01 — no GlobalScale ran");

    const iris::ModelPreRead m = iris::ModelPreRead::read(cubeM);
    CHECK(m.parsed && near_(m.declaredUnitScale, 1.0, 1e-9),
          "unit_cube_m.fbx declares 1 unit = 1 m");

    const iris::ModelPreRead rigged = iris::ModelPreRead::read(rig);
    CHECK(rigged.parsed, "the pre-read parses rig2.glb");
    CHECK(rigged.rigged(), "…and sees its bones (a character)");
    std::printf("    rig2: %d bones, %d clips, box y %g\n", rigged.bones,
                int(rigged.clipNames.size()), rigged.aabbMax[1] - rigged.aabbMin[1]);

    // A format hint reads the same bytes off a path that does not name them —
    // the reimport case, where the store's objects are named by content hash.
    {
        const iris::ModelPreRead hinted = iris::ModelPreRead::read(cubeCm, QStringLiteral("fbx"));
        CHECK(hinted.parsed && near_(hinted.declaredUnitScale, cm.declaredUnitScale, 1e-12)
                  && near_(hinted.aabbMax[1] - hinted.aabbMin[1], rawY, 1e-9),
              "a format-hinted pre-read reads exactly the same file");
    }

    // ---- 2. THE PREVIEW ARITHMETIC ----------------------------------------
    {
        iris::ImportSettings s;      // identity
        const auto box = ImportSettingsDialog::placedBox(s, cm);
        CHECK(near_(box.size(1), 0.01, 1e-5),
              "identity settings: a 1-unit cube declared at cm measures 0.01 m");

        s.units = iris::ImportSettings::Units::Metres;
        CHECK(near_(ImportSettingsDialog::placedBox(s, cm).size(1), 1.0, 1e-4),
              "units:\"m\" OVERRIDES the file's declaration: the same cube is 1 m");

        s.units = iris::ImportSettings::Units::Auto;
        s.scale = 2.0;
        CHECK(near_(ImportSettingsDialog::placedBox(s, cm).size(1), 0.02, 1e-5),
              "scale 2 doubles it (0.02 m)");

        // A signed-permutation axes fix swaps the extents and nothing else.
        iris::ImportSettings zup;
        zup.up = QStringLiteral("+Z");
        zup.forward = QStringLiteral("+Y");
        const auto plain = ImportSettingsDialog::placedBox(iris::ImportSettings(), rigged);
        const auto swapped = ImportSettingsDialog::placedBox(zup, rigged);
        CHECK(near_(swapped.size(1), plain.size(2), 1e-4)
                  && near_(swapped.size(2), plain.size(1), 1e-4),
              "a +Z-up file's Y and Z extents swap when the axes are fixed");
    }

    // ---- 3. THE ORIGIN HELPERS --------------------------------------------
    {
        ImportSettingsDialog::Box box;
        box.valid = true;
        box.min[0] = 1.0;  box.max[0] = 3.0;      // centre 2
        box.min[1] = 10.0; box.max[1] = 12.0;     // centre 11, bottom 10
        box.min[2] = -4.0; box.max[2] = 0.0;      // centre -2
        double t[3];
        ImportSettingsDialog::originTranslation(ImportSettingsDialog::Origin::Keep, box, t);
        CHECK(t[0] == 0.0 && t[1] == 0.0 && t[2] == 0.0, "Keep translates by nothing");
        ImportSettingsDialog::originTranslation(ImportSettingsDialog::Origin::Centre, box, t);
        CHECK(near_(t[0], -2.0) && near_(t[1], -11.0) && near_(t[2], 2.0),
              "Centre puts the box's centre on the origin");
        ImportSettingsDialog::originTranslation(ImportSettingsDialog::Origin::BottomCentre, box, t);
        CHECK(near_(t[0], -2.0) && near_(t[1], -10.0) && near_(t[2], 2.0),
              "Bottom-centre stands it on the ground, centred in X and Z");

        iris::ImportSettings s;
        CHECK(ImportSettingsDialog::originOf(s, box) == ImportSettingsDialog::Origin::Keep,
              "a zero translation reads back as Keep");
        s.translate[0] = -2.0; s.translate[1] = -10.0; s.translate[2] = 2.0;
        CHECK(ImportSettingsDialog::originOf(s, box)
                  == ImportSettingsDialog::Origin::BottomCentre,
              "…and the bottom-centre offset reads back as Bottom-centre");
        s.translate[1] = -9.5;
        CHECK(ImportSettingsDialog::originOf(s, box) == ImportSettingsDialog::Origin::Custom,
              "…while a hand-typed offset reads back as Custom");
    }

    // ---- 4. THE SUGGESTION LINE, THREE STATES -----------------------------
    {
        QString text;
        // Not a character: nothing to say about a crate.
        CHECK(ImportSettingsDialog::suggestionFor(iris::ImportSettings(), cm, &text)
                  == ImportSettingsDialog::Suggestion::None
                  && text.isEmpty(),
              "a file with no bones gets NO suggestion");

        iris::ImportSettings s;
        const double height = ImportSettingsDialog::placedBox(s, rigged).size(1);
        CHECK(ImportSettingsDialog::suggestionFor(s, rigged, &text)
                  == ImportSettingsDialog::Suggestion::Plausible
                  && text.contains(QStringLiteral("plausible")),
              qPrintable(QStringLiteral("a %1 m character reads as plausible: \"%2\"")
                             .arg(height).arg(text)));

        // THE CLASSIC WRONG UNIT: a character authored in centimetres in a
        // file that declares metres reads 175 m tall. The line says WHICH unit
        // would land it in the envelope — and still does not apply it.
        iris::ModelPreRead cmCharacter;
        cmCharacter.parsed = true;
        cmCharacter.bones = 34;
        cmCharacter.declaredUnitScale = 1.0;
        cmCharacter.aabbValid = true;
        cmCharacter.aabbMin[0] = -25; cmCharacter.aabbMax[0] = 25;
        cmCharacter.aabbMin[1] = 0;   cmCharacter.aabbMax[1] = 175;
        cmCharacter.aabbMin[2] = -20; cmCharacter.aabbMax[2] = 20;
        CHECK(ImportSettingsDialog::suggestionFor(iris::ImportSettings(), cmCharacter, &text)
                  == ImportSettingsDialog::Suggestion::TooLarge,
              qPrintable(QStringLiteral("a 175 m character reads as unusual: \"%1\"")
                             .arg(text)));
        CHECK(text.contains(QStringLiteral("Centimetres")),
              "…and names the unit that would land it in the envelope (cm)");

        s.scale = 0.01;
        CHECK(ImportSettingsDialog::suggestionFor(s, rigged, &text)
                  == ImportSettingsDialog::Suggestion::TooSmall,
              qPrintable(QStringLiteral("x0.01 reads as unusual the other way: \"%1\"")
                             .arg(text)));
        CHECK(!text.contains(QStringLiteral("Try")),
              "…with NO unit named, because at x0.01 no unit is the cure — the SCALE is");

        // It NEVER applies itself: the record is untouched by asking.
        iris::ImportSettings before = s;
        ImportSettingsDialog::suggestionFor(s, rigged, &text);
        CHECK(before.canonicalJson() == s.canonicalJson(),
              "asking for the suggestion changes nothing in the record");
    }

    // ---- 5. THE DIALOG'S FIELDS -> THE RECORD, BYTE FOR BYTE --------------
    {
        ImportSettingsDialog dialog;
        dialog.setMode(ImportSettingsDialog::Mode::Import);
        iris::ModelPreRead facts = rigged;
        dialog.setPreRead(facts);

        iris::ImportSettings wanted;
        wanted.scale = 2.5;
        wanted.units = iris::ImportSettings::Units::Centimetres;
        wanted.up = QStringLiteral("+Z");
        wanted.forward = QStringLiteral("+Y");
        wanted.rotate[1] = 90.0;
        wanted.translate[0] = 1.5;
        wanted.skeleton = false;
        wanted.materials = iris::ImportSettings::MaterialMode::None;
        dialog.setSettings(wanted);

        CHECK(dialog.record() == wanted.toJson(),
              "setSettings -> record() round-trips the whole record");
        CHECK(dialog.settings().canonicalJson() == wanted.canonicalJson(),
              "…and its CANONICAL form is byte-identical — the same bake key");
        CHECK(dialog.settings().hash() == wanted.hash(),
              "…which is to say: the same hash a verb import would key");

        // Now the other direction: TOUCH THE WIDGETS and read the record back.
        auto *scale = nth<QDoubleSpinBox>(&dialog, 0);
        CHECK(scale && near_(scale->value(), 2.5),
              "the scale field shows what was set");
        if (scale) scale->setValue(4.0);
        CHECK(near_(dialog.settings().scale, 4.0, 1e-9),
              "typing in the scale field writes the record");

        auto *materials = dialog.findChildren<QCheckBox *>().value(1);
        if (materials) materials->setChecked(true);
        CHECK(dialog.settings().materials == iris::ImportSettings::MaterialMode::Import,
              "ticking Materials writes 'import' into the record");

        iris::ImportSettings expect = wanted;
        expect.scale = 4.0;
        expect.materials = iris::ImportSettings::MaterialMode::Import;
        CHECK(dialog.settings().canonicalJson() == expect.canonicalJson(),
              "and the whole record is still exactly the expected canonical form");
    }

    // ---- 6. THE AXES CANNOT BE MADE PARALLEL ------------------------------
    {
        ImportSettingsDialog dialog;
        const auto combos = dialog.findChildren<QComboBox *>();
        // units, up, forward, origin, clip mode — the two axis combos are the
        // only ones offering "+Y".
        QComboBox *up = nullptr, *forward = nullptr;
        for (QComboBox *c : combos) {
            if (c->findData(QStringLiteral("+Y")) < 0) continue;
            if (!up) up = c; else if (!forward) forward = c;
        }
        CHECK(up && forward, "the dialog has an up and a forward axis combo");
        if (up && forward) {
            up->setCurrentIndex(up->findData(QStringLiteral("-Z")));
            const iris::ImportSettings s = dialog.settings();
            iris::Vec3 u, f;
            iris::ImportSettings::axisFromName(s.up, &u);
            iris::ImportSettings::axisFromName(s.forward, &f);
            CHECK(std::fabs(double(iris::Vec3::dotProduct(u, f))) < 1e-4,
                  qPrintable(QStringLiteral("choosing up=-Z moves forward off it (up %1, "
                                            "forward %2)").arg(s.up, s.forward)));
            QString error;
            iris::ImportSettings::fromJson(s.toJson(), &error);
            CHECK(error.isEmpty(),
                  "…so the record the dialog holds is one the pipeline accepts");
        }
    }

    // ---- 7. THE CLIP CHECKLIST --------------------------------------------
    {
        ImportSettingsDialog dialog;
        dialog.setPreRead(rigged);
        QComboBox *clipMode = nullptr;
        for (QComboBox *c : dialog.findChildren<QComboBox *>())
            if (c->count() == 3 && c->itemText(1) == QStringLiteral("None")) clipMode = c;
        CHECK(clipMode, "the dialog has an All/None/Choose clip row");
        if (clipMode) {
            clipMode->setCurrentIndex(1);
            CHECK(dialog.settings().clips == false && dialog.settings().clipNames.isEmpty(),
                  "\"None\" records clips:false");
            clipMode->setCurrentIndex(0);
            CHECK(dialog.settings().clips == true && dialog.settings().clipNames.isEmpty(),
                  "\"All\" records clips:true with no name list");
        }
        auto *list = dialog.findChild<QListWidget *>();
        CHECK(list && list->count() == rigged.clipNames.size(),
              "the checklist shows exactly the clips the pre-read found");
        if (clipMode && list && list->count() > 0) {
            clipMode->setCurrentIndex(2);
            for (int i = 0; i < list->count(); ++i)
                list->item(i)->setCheckState(i == 0 ? Qt::Checked : Qt::Unchecked);
            const iris::ImportSettings s = dialog.settings();
            CHECK(s.clipNames.size() == 1 && s.clipNames.first() == rigged.clipNames.first(),
                  "…and ticking one writes exactly its name into the record");
            CHECK(s.wantsClip(rigged.clipNames.first()),
                  "…a name the filter then matches (one naming rule, import/clipnaming.h)");
            for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Unchecked);
            CHECK(dialog.settings().clips == false,
                  "…and ticking NOTHING is \"no clips\", not \"all clips\"");
        }
    }

    // ---- 7b. A RECORD IS NOT LOST WHILE THE PRE-READ IS IN FLIGHT ---------
    // Reimport mode fills the fields from the stored record LONG before the
    // file has been read: an empty clip list and a scale the spin box cannot
    // spell must not quietly rewrite what the asset already carries.
    {
        ImportSettingsDialog dialog;            // no setPreRead: nothing read yet
        dialog.setMode(ImportSettingsDialog::Mode::Reimport);
        iris::ImportSettings stored;
        stored.clipNames = QStringList{ QStringLiteral("Walk") };
        stored.scale = 0.000037;                // finer than the field's 4 decimals
        dialog.setSettings(stored);

        // TOUCH AN UNRELATED FIELD FOR REAL. setValue(value()) emits nothing
        // (measured: 0 valueChanged), so readFieldsIntoSettings would never run
        // and this case would pass with both guards deleted — which is what the
        // second read caught. A checkbox toggled and toggled back is two real
        // signals and leaves the record's own value alone.
        auto *skeleton = dialog.findChildren<QCheckBox *>().value(0);
        CHECK(skeleton && skeleton->isChecked(), "the skeleton box is on, as the record says");
        if (skeleton) { skeleton->setChecked(false); skeleton->setChecked(true); }
        const iris::ImportSettings now = dialog.settings();
        CHECK(now.clipNames == stored.clipNames && now.clips,
              "an unread clip list does not erase the record's clip choice");
        CHECK(near_(now.scale, stored.scale, 1e-12),
              qPrintable(QStringLiteral("…and the scale survives to the last digit (%1)")
                             .arg(now.scale, 0, 'g', 12)));
        CHECK(dialog.settings().canonicalJson() == stored.canonicalJson(),
              "…so the whole record is still byte-identical to the stored one");
    }

    // ---- 7c. AN ORIGIN HELPER FOLLOWS THE BOX ------------------------------
    // The normal order of work is the one that used to break it: pick
    // Bottom-centre, THEN fix the unit the suggestion line just told you about.
    // Every field that moves the box (units, scale, the axes, the rotation) has
    // to move the helper's offset with it, or the combo goes on claiming
    // "Bottom centre" while the model floats.
    {
        ImportSettingsDialog dialog;
        dialog.setPreRead(cm);                          // a cm-declared cube
        dialog.setOrigin(ImportSettingsDialog::Origin::BottomCentre);
        const auto expect = [&]() {
            double want[3];
            ImportSettingsDialog::originTranslation(
                ImportSettingsDialog::Origin::BottomCentre,
                ImportSettingsDialog::placedBox(dialog.settings(), cm), want);
            const iris::ImportSettings s = dialog.settings();
            return near_(s.translate[0], want[0], 1e-9) && near_(s.translate[1], want[1], 1e-9)
                   && near_(s.translate[2], want[2], 1e-9);
        };
        CHECK(expect(), "Bottom-centre sits the cube on the ground as imported");
        // X, not Y: this cube's box starts at y = 0, so its BOTTOM offset is
        // zero at every scale — the X centring is what visibly moves.
        const double first = dialog.settings().translate[0];

        QComboBox *units = nullptr;
        for (QComboBox *c : dialog.findChildren<QComboBox *>())
            if (c->findData(QStringLiteral("cm")) >= 0) units = c;
        CHECK(units, "the dialog has a units combo");
        if (units) units->setCurrentIndex(units->findData(QStringLiteral("m")));
        CHECK(expect(), "…and FOLLOWS a unit change (m: the box is 100x bigger)");
        CHECK(!near_(dialog.settings().translate[0], first, 1e-9),
              qPrintable(QStringLiteral("…which means it MOVED (%1 -> %2)")
                             .arg(first).arg(dialog.settings().translate[0])));
        CHECK(dialog.origin() == ImportSettingsDialog::Origin::BottomCentre,
              "…and the combo still, honestly, says Bottom centre");

        auto *scale = nth<QDoubleSpinBox>(&dialog, 0);
        if (scale) scale->setValue(2.0);
        CHECK(expect(), "…and follows a SCALE change too");

        auto *rot = dialog.findChildren<QDoubleSpinBox *>().value(1);   // rotation X
        if (rot) rot->setValue(90.0);
        CHECK(expect(), "…and a rotation, which was the one case that already worked");
    }

    // ---- 7d. A HELPER'S OFFSET SURVIVES THE FIELD --------------------------
    // originTranslation writes on the offset field's own grid, so a record
    // written by a helper reads back AS that helper — a dialog that forgot its
    // own decision on reopening would send every user to "Custom".
    {
        ImportSettingsDialog dialog;
        dialog.setPreRead(rigged);
        dialog.setOrigin(ImportSettingsDialog::Origin::Centre);
        const iris::ImportSettings written = dialog.settings();

        ImportSettingsDialog reopened;               // as a reimport would
        reopened.setSettings(written);
        reopened.setPreRead(rigged);
        CHECK(reopened.origin() == ImportSettingsDialog::Origin::Centre,
              "a Centre offset written by the dialog reads back as Centre, not Custom");
        CHECK(reopened.settings().canonicalJson() == written.canonicalJson(),
              "…and reopening changed nothing in the record");

        // …AND ON A BOX WHOSE CENTRE IS NOT A ROUND NUMBER, which is the case
        // the offset field's decimals would otherwise eat.
        iris::ModelPreRead awkward;
        awkward.parsed = true;
        awkward.aabbValid = true;
        awkward.aabbMin[0] = -0.3711117; awkward.aabbMax[0] = 0.9134449;
        awkward.aabbMin[1] = 0.0193337;  awkward.aabbMax[1] = 1.7712223;
        awkward.aabbMin[2] = -0.5511119; awkward.aabbMax[2] = 0.1233331;
        ImportSettingsDialog odd;
        odd.setPreRead(awkward);
        odd.setOrigin(ImportSettingsDialog::Origin::BottomCentre);
        const iris::ImportSettings oddWritten = odd.settings();
        ImportSettingsDialog oddReopened;
        oddReopened.setSettings(oddWritten);
        oddReopened.setPreRead(awkward);
        CHECK(oddReopened.origin() == ImportSettingsDialog::Origin::BottomCentre,
              qPrintable(QStringLiteral("…and on an awkward box too (offset %1, %2, %3)")
                             .arg(oddWritten.translate[0], 0, 'g', 10)
                             .arg(oddWritten.translate[1], 0, 'g', 10)
                             .arg(oddWritten.translate[2], 0, 'g', 10)));
        CHECK(oddReopened.settings().canonicalJson() == oddWritten.canonicalJson(),
              "…byte for byte");
    }

    // ---- 7e. THE SAME CLIPS ARE THE SAME RECORD ----------------------------
    // The checklist is built in FILE order with the FILE's spelling while
    // wantsClip matches case-insensitively, so a stored list in another order
    // or case would come back rewritten — a moved hash, and a re-bake of an
    // asset nobody changed.
    if (rigged.clipNames.size() >= 2) {
        iris::ImportSettings stored;
        stored.clipNames = QStringList{ rigged.clipNames.at(1).toUpper(),
                                        rigged.clipNames.at(0).toLower() };
        ImportSettingsDialog dialog;
        dialog.setSettings(stored);
        dialog.setPreRead(rigged);
        auto *skeleton = dialog.findChildren<QCheckBox *>().value(0);
        if (skeleton) { skeleton->setChecked(false); skeleton->setChecked(true); }
        CHECK(dialog.settings().clipNames == stored.clipNames,
              qPrintable(QStringLiteral("a stored clip list keeps its order and spelling "
                                        "(%1 vs %2)")
                             .arg(dialog.settings().clipNames.join(','),
                                  stored.clipNames.join(','))));
        CHECK(dialog.settings().hash() == stored.hash(),
              "…so the bake key does not move and nothing is re-baked");
    }

    // ---- 7f. A ROTATION THE FIELD CANNOT SPELL -----------------------------
    {
        iris::ImportSettings stored;
        stored.rotate[1] = 33.333;                 // against the field's 2 decimals
        stored.translate[0] = 0.0001234;           // against the offset field's
        ImportSettingsDialog dialog;
        dialog.setSettings(stored);
        auto *skeleton = dialog.findChildren<QCheckBox *>().value(0);
        if (skeleton) { skeleton->setChecked(false); skeleton->setChecked(true); }
        CHECK(dialog.settings().canonicalJson() == stored.canonicalJson(),
              "a rotation and an offset finer than the fields' decimals survive a touch");
    }

    // ---- 7g. A SOURCE THAT CANNOT BE READ IS REFUSED, NOT ATTEMPTED --------
    // A .gltf whose geometry lives in a sibling .bin: the library stores only
    // the file that was imported, so a read of the STORED bytes cannot find the
    // sibling (measured, IMPORT-2 F7 — the reimport verb fails the same way one
    // level down). The dialog says so instead of offering an OK that cannot
    // work.
    {
        QTemporaryDir alone;
        const QString lonely = QDir(alone.path()).filePath(QStringLiteral("orphan.gltf"));
        QFile::copy(QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR
                                   "/tests/importer/fixtures/external_buffer.gltf"), lonely);
        const iris::ModelPreRead orphan = iris::ModelPreRead::read(lonely);
        CHECK(!orphan.parsed, "a .gltf without its .bin cannot be read");
        std::printf("    orphan error: %s\n", qPrintable(orphan.error));

        ImportSettingsDialog dialog;
        dialog.setMode(ImportSettingsDialog::Mode::Reimport);
        dialog.startPreRead(lonely);
        // the worker's answer, on this thread
        for (int i = 0; i < 400 && dialog.isBusy(); ++i) {
            QApplication::processEvents();
            QThread::msleep(5);
        }
        CHECK(!dialog.isBusy(), "the pre-read finished");
        CHECK(dialog.sourceUnreadable(), "…and the dialog knows the source could not be read");
        CHECK(dialog.statusText().contains(QStringLiteral("cannot be reimported")),
              qPrintable(QStringLiteral("…and says so: \"%1\"")
                             .arg(dialog.statusText().simplified())));
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        CHECK(buttons && !buttons->button(QDialogButtonBox::Ok)->isEnabled(),
              "…with OK refused, rather than failing one level down");

        // The same file WITH its sibling reads fine — so the refusal is about
        // the missing file, not about the fixture.
        const iris::ModelPreRead whole = iris::ModelPreRead::read(
            QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR
                           "/tests/importer/fixtures/external_buffer.gltf"));
        CHECK(whole.parsed && near_(whole.aabbMax[1] - whole.aabbMin[1], 1.0, 1e-4),
              "the same .gltf beside its .bin reads as a 1 m cube");
    }

    // ---- 8. THE BATCH STATE MACHINE ---------------------------------------
    {
        const QStringList files = { QStringLiteral("a.fbx"), QStringLiteral("b.glb"),
                                    QStringLiteral("c.obj") };
        iris::ImportSettings twice;
        twice.scale = 2.0;

        // (a) answered one by one
        {
            ImportSettingsBatch batch;
            batch.setFiles(files);
            CHECK(batch.current() == QStringLiteral("a.fbx") && batch.remaining() == 2,
                  "the batch starts on the first file with two to come");
            CHECK(batch.needsPrompt(), "…and it needs an answer");
            batch.accept(iris::ImportSettings().toJson(), false);
            CHECK(batch.current() == QStringLiteral("b.glb") && batch.remaining() == 1
                      && batch.needsPrompt(),
                  "…then the next one, still asking");
            batch.accept(twice.toJson(), false);
            batch.accept(iris::ImportSettings().toJson(), false);
            CHECK(batch.atEnd() && batch.accepted().size() == 3,
                  "three answers finish a three-file batch");
            CHECK(batch.recordFor(QStringLiteral("b.glb")) == twice.toJson(),
                  "…each file keeping its own record");
        }

        // (b) "use these for the remaining N"
        {
            ImportSettingsBatch batch;
            batch.setFiles(files);
            batch.accept(twice.toJson(), true);
            CHECK(!batch.needsPrompt(),
                  "\"use these for the remaining\" stops the asking");
            batch.takeSticky();
            batch.takeSticky();
            CHECK(batch.atEnd() && batch.accepted().size() == 3,
                  "…and the rest of the batch takes the same answer");
            CHECK(batch.recordFor(QStringLiteral("c.obj")) == twice.toJson(),
                  "…byte for byte");
            CHECK(batch.skipped().isEmpty(), "…with nothing skipped");
        }

        // (b2) "Skip the rest" — the third answer (§12.5 is one dialog per
        // model file, so a ten-file drop needs an exit that is not ten Cancels)
        {
            ImportSettingsBatch batch;
            batch.setFiles(files);
            batch.accept(twice.toJson(), false);
            batch.skipAll();
            CHECK(batch.atEnd(), "\"Skip the rest\" finishes the batch");
            CHECK(batch.accepted() == QStringList{ QStringLiteral("a.fbx") },
                  "…keeping what was already answered");
            CHECK(batch.skipped().size() == 2,
                  "…and skipping the whole tail, not just the current file");
        }

        // (c) Cancel skips THAT FILE ONLY
        {
            ImportSettingsBatch batch;
            batch.setFiles(files);
            batch.skip();
            CHECK(batch.current() == QStringLiteral("b.glb"),
                  "Cancel moves on to the next file");
            batch.accept(twice.toJson(), false);
            batch.accept(twice.toJson(), false);
            CHECK(batch.skipped() == QStringList{ QStringLiteral("a.fbx") },
                  "…exactly one file was skipped");
            CHECK(batch.accepted().size() == 2 && batch.atEnd(),
                  "…and the other two still import");
            CHECK(batch.recordFor(QStringLiteral("a.fbx")).isEmpty(),
                  "…the skipped file has no record at all");
        }
    }

    // ---- 9. THE BUTTONS SAY WHAT THEY DO ----------------------------------
    {
        ImportSettingsDialog dialog;
        dialog.setMode(ImportSettingsDialog::Mode::Import);
        dialog.setRemaining(3);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        CHECK(buttons && buttons->button(QDialogButtonBox::Cancel)->text()
                             .contains(QStringLiteral("Skip")),
              "in a batch, Cancel says it skips THIS FILE");
        auto *remainingBox = dialog.findChildren<QCheckBox *>().value(2);
        CHECK(remainingBox && remainingBox->isVisibleTo(&dialog),
              "…and the \"use for the remaining\" box is offered");
        dialog.setRemaining(0);
        CHECK(buttons && buttons->button(QDialogButtonBox::Cancel)->text()
                             == QObject::tr("Cancel"),
              "…on the last file it is an ordinary Cancel");
        CHECK(!dialog.applyToRemaining(),
              "…and there is nothing left to apply settings to");

        dialog.setMode(ImportSettingsDialog::Mode::Reimport);
        CHECK(buttons && buttons->button(QDialogButtonBox::Ok)->text()
                             .contains(QStringLiteral("Reimport")),
              "in reimport mode the OK button says Reimport");
    }

    // ---- 10. REIMPORT MODE COMMITS THROUGH ITS HANDLER --------------------
    {
        ImportSettingsDialog dialog;
        dialog.setMode(ImportSettingsDialog::Mode::Reimport);
        iris::ImportSettings s;
        s.scale = 3.0;
        dialog.setSettings(s);

        QJsonObject committed;
        int calls = 0;
        bool answer = true;
        dialog.setCommitHandler([&](const QJsonObject &record, QString *error) {
            ++calls;
            committed = record;
            if (!answer) { *error = QStringLiteral("no"); return false; }
            return true;
        });

        answer = false;
        dialog.accept();
        CHECK(calls == 1 && dialog.result() != QDialog::Accepted,
              "a refused commit keeps the dialog open — the settings stay the user's to fix");
        answer = true;
        dialog.accept();
        CHECK(calls == 2 && committed == s.toJson(),
              "OK hands the record to the commit handler, unchanged");
    }

    // A PICTURE OF THE DIALOG, on request. A pixel-visible feature needs pixel
    // evidence, and this is a widget dialog: a grab of the real widget tree is
    // the honest article. Off by default — set JAH_IMPORT_DIALOG_SHOT to a path
    // to write one.
    const QByteArray shot = qgetenv("JAH_IMPORT_DIALOG_SHOT");
    if (!shot.isEmpty()) {
        ImportSettingsDialog dialog;
        dialog.setMode(ImportSettingsDialog::Mode::Import);
        dialog.setSubject(QStringLiteral("Jennifer.fbx"));
        dialog.setRemaining(2);
        dialog.setPreRead(rigged);
        dialog.resize(520, dialog.sizeHint().height());
        dialog.show();
        QApplication::processEvents();
        const bool wrote = dialog.grab().save(QString::fromLocal8Bit(shot));
        std::printf("    %s %s\n", wrote ? "wrote" : "FAILED to write", shot.constData());
    }

    std::printf(failures ? "ui.import_dialog: %d FAILURES\n" : "ui.import_dialog: all ok\n",
                failures);
    return failures == 0 ? 0 : 1;
}
