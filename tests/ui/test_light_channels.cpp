// ui.light_channels — the lighting-channel checkbox row (LightChannelsWidget).
//
// The row edits EIGHT checkboxes over a THIRTY-TWO bit document field, which is
// the whole reason it needs a test: everything interesting about it is what it
// does with the 24 bits it does not show.
//
//   * it must PRESERVE them (a script may set channel 20; ticking box 3 in the
//     panel must not silently wipe it),
//   * "All" must restore the real all-ones default, upper bits included, since
//     it is the "turn this off again" button,
//   * setMask must be QUIET — the panel calls it while SELECTING a node, so an
//     emitting setter would write the node's own value back into it through the
//     slot, which is how selection-time feedback loops are born.
//
// Widgets only: no document, no engine, no display (offscreen QPA).
#include "ui/controls/lightchannelswidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

class LightChannelsTest : public QObject
{
    Q_OBJECT

private:
    /// The eight channel boxes, in order. They are the only QCheckBoxes the row
    /// builds, and QObject::findChildren preserves construction order.
    static QList<QCheckBox *> boxes(LightChannelsWidget &w)
    {
        return w.findChildren<QCheckBox *>();
    }

    static QPushButton *button(LightChannelsWidget &w, const QString &text)
    {
        for (QPushButton *b : w.findChildren<QPushButton *>())
            if (b->text() == text) return b;
        return nullptr;
    }

private slots:
    void defaults_are_all_channels()
    {
        LightChannelsWidget w;
        QCOMPARE(w.mask(), 0xFFFFFFFFu);
        const auto b = boxes(w);
        QCOMPARE(b.size(), 8);
        for (QCheckBox *box : b) QVERIFY2(box->isChecked(), "every channel starts ticked");
    }

    void set_mask_is_quiet()
    {
        LightChannelsWidget w;
        QSignalSpy spy(&w, &LightChannelsWidget::maskChanged);
        w.setMask(0x2u);
        QCOMPARE(w.mask(), 0x2u);
        QCOMPARE(spy.count(), 0);      // the panel calls this while selecting a node
        const auto b = boxes(w);
        QVERIFY(b[1]->isChecked());
        QVERIFY(!b[0]->isChecked());
        QVERIFY(!b[7]->isChecked());
    }

    void ticking_a_box_emits_the_new_mask()
    {
        LightChannelsWidget w;
        w.setMask(0u);
        QSignalSpy spy(&w, &LightChannelsWidget::maskChanged);
        boxes(w)[3]->setChecked(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toUInt(), 0x8u);
        QCOMPARE(w.mask(), 0x8u);
        boxes(w)[3]->setChecked(false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(w.mask(), 0u);
    }

    void the_upper_24_bits_survive_an_edit()
    {
        // Channel 20 is not on the row. Ticking channel 0 must add channel 0,
        // not replace the mask with it.
        LightChannelsWidget w;
        w.setMask(1u << 20);
        QSignalSpy spy(&w, &LightChannelsWidget::maskChanged);
        boxes(w)[0]->setChecked(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(w.mask(), (1u << 20) | 1u);
        // ...and un-ticking a visible box does not take the invisible one with it.
        boxes(w)[0]->setChecked(false);
        QCOMPARE(w.mask(), 1u << 20);
    }

    void all_and_none_are_the_two_ends()
    {
        LightChannelsWidget w;
        w.setMask(1u << 20);
        QSignalSpy spy(&w, &LightChannelsWidget::maskChanged);
        QPushButton *all = button(w, QStringLiteral("All"));
        QVERIFY(all);
        all->click();
        // The FULL default, upper bits included: "All" is how a user turns the
        // feature off again, and leaving a stray bit set would leave the node
        // quietly filtered.
        QCOMPARE(w.mask(), 0xFFFFFFFFu);
        QPushButton *none = button(w, QStringLiteral("None"));
        QVERIFY(none);
        none->click();
        QCOMPARE(w.mask(), 0u);
        QCOMPARE(spy.count(), 2);
        // Clicking "None" twice is a no-op, not a second signal.
        none->click();
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(LightChannelsTest)
#include "test_light_channels.moc"
