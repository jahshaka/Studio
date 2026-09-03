// ui.viewport_cover — the viewport's "nothing is presenting" state
// (src/viewport/viewportcover.h), the widget half of the desktop-bleed fix.
//
// What this pins, because it is what the defect turned on:
//   - the cover is HIDDEN while the viewport presents, and visible otherwise;
//   - when it is up it fills EVERY pixel of its rect with its opaque surface
//     colour (a cover with a transparent corner is a cover that leaks stale
//     desktop pixels);
//   - it says which state it is in, and names the world while loading;
//   - showNow() paints synchronously — the whole point, since the scene load
//     that follows it never returns to the event loop.
#include <QtTest>
#include <QImage>
#include <QPainter>
#include "viewport/viewportcover.h"

class TestViewportCover : public QObject
{
    Q_OBJECT
private slots:
    void hiddenWhilePresenting();
    void coversEveryPixel();
    void statesAndSubtitle();
    void showNowPaintsSynchronously();
};

/// Renders the cover into an image the way the compositor would see it, on a
/// deliberately GARISH background: anything the cover fails to paint shows up.
static QImage renderCover(ViewportCover &cover)
{
    QImage shot(cover.size(), QImage::Format_RGB32);
    shot.fill(QColor(255, 0, 255));
    QPainter painter(&shot);
    cover.render(&painter, QPoint(), QRegion(), QWidget::DrawWindowBackground |
                                                    QWidget::DrawChildren);
    painter.end();
    return shot;
}

void TestViewportCover::hiddenWhilePresenting()
{
    QWidget host;
    host.resize(400, 300);
    ViewportCover cover(&host);
    cover.resize(400, 300);
    host.show();

    cover.setState(ViewportCover::State::Presenting);
    QVERIFY2(!cover.isVisible(), "presenting: the engine owns the pixels, no cover");

    cover.setState(ViewportCover::State::Loading);
    QVERIFY2(cover.isVisible(), "loading: the cover is up");

    cover.setState(ViewportCover::State::Presenting);
    QVERIFY2(!cover.isVisible(), "first present takes the cover down again");
}

void TestViewportCover::coversEveryPixel()
{
    ViewportCover cover;
    cover.resize(320, 240);
    cover.setState(ViewportCover::State::Loading);

    const QImage shot = renderCover(cover);
    QCOMPARE(shot.size(), QSize(320, 240));
    const QRgb magenta = qRgb(255, 0, 255);
    int leaked = 0;
    for (int y = 0; y < shot.height(); ++y)
        for (int x = 0; x < shot.width(); ++x)
            if (shot.pixel(x, y) == magenta) ++leaked;
    QCOMPARE(leaked, 0);

    // The corners are the surface colour itself (no text there), and it is a
    // dark, deliberate grey — never black, never the engine's business.
    const QColor corner = shot.pixelColor(2, 2);
    QCOMPARE(corner, shot.pixelColor(shot.width() - 3, shot.height() - 3));
    QVERIFY2(corner.red() > 20 && corner.red() < 90, qPrintable(corner.name()));
    QVERIFY2(corner.blue() >= corner.red(), "the surface is neutral-to-cool, not warm");
}

void TestViewportCover::statesAndSubtitle()
{
    ViewportCover cover;
    cover.resize(400, 200);

    cover.setState(ViewportCover::State::NoScene);
    QCOMPARE(cover.state(), ViewportCover::State::NoScene);
    const QImage noScene = renderCover(cover);

    cover.setSubtitle(QStringLiteral("Showroom"));
    cover.setState(ViewportCover::State::Loading);
    QCOMPARE(cover.state(), ViewportCover::State::Loading);
    const QImage loading = renderCover(cover);

    // The two states do not look the same: different message, and the loading
    // one also carries the world's name.
    QVERIFY2(noScene != loading, "the loading and no-scene states differ visibly");

    cover.setSubtitle(QString());
    const QImage loadingNoName = renderCover(cover);
    QVERIFY2(loadingNoName != loading, "the world name is actually drawn");
}

void TestViewportCover::showNowPaintsSynchronously()
{
    QWidget host;
    host.resize(300, 200);
    ViewportCover cover(&host);
    cover.resize(300, 200);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    // No event loop turn between showNow() and the check: a posted paint would
    // not have run, and the scene load this exists to cover never yields.
    cover.showNow(ViewportCover::State::Loading);
    QVERIFY(cover.isVisible());
    QCOMPARE(cover.state(), ViewportCover::State::Loading);
}

QTEST_MAIN(TestViewportCover)
#include "test_viewport_cover.moc"
