// Generator for tiny.mp4 (and the ASSET_MEDIA_SPEC §3 spike clips): pushes
// painted frames through QMediaRecorder (Qt 6.10 ffmpeg backend) — no
// external encoder needed. Not built by CMake; compile ad hoc:
//   g++ -fPIC genmp4.cpp -o genmp4 $(pkg-config --cflags --libs Qt6Multimedia Qt6Gui Qt6Core)
//   QT_QPA_PLATFORM=offscreen ./genmp4 tiny.mp4          # 64x64, 16 frames @ 8 fps
//   QT_QPA_PLATFORM=offscreen ./genmp4 f1080.mp4 1920 1080 90 30
// Usage: genmp4 out.mp4 [w h frames fps]
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>
#include <QMediaFormat>
#include <QVideoFrame>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QDebug>

int main(int argc, char** argv)
{
    qputenv("QT_LOGGING_RULES", "qt.multimedia.*=false");
    QGuiApplication app(argc, argv);
    if (argc < 2) { qWarning("usage: genmp4 out.mp4 [w h frames fps]"); return 2; }
    const QString outPath = QString::fromLocal8Bit(argv[1]);
    const int W = argc > 2 ? atoi(argv[2]) : 64;
    const int H = argc > 3 ? atoi(argv[3]) : 64;
    const int NF = argc > 4 ? atoi(argv[4]) : 16;
    const int FPS = argc > 5 ? atoi(argv[5]) : 8;

    QMediaCaptureSession session;
    QVideoFrameInput input;
    QMediaRecorder recorder;
    session.setVideoFrameInput(&input);
    session.setRecorder(&recorder);

    QMediaFormat fmt(QMediaFormat::MPEG4);
    fmt.setVideoCodec(QMediaFormat::VideoCodec::MPEG4);
    recorder.setMediaFormat(fmt);
    recorder.setVideoResolution(W, H);
    recorder.setVideoFrameRate(FPS);
    recorder.setOutputLocation(QUrl::fromLocalFile(outPath));

    int framesSent = 0;
    const int totalFrames = NF;
    auto sendFrames = [&]() {
        while (framesSent < totalFrames) {
            QImage img(W, H, QImage::Format_RGBA8888);
            QPainter p(&img);
            p.fillRect(img.rect(), QColor::fromHsv((framesSent * 20) % 360, 200, 230));
            p.setPen(Qt::white);
            p.drawText(img.rect(), Qt::AlignCenter, QString::number(framesSent));
            p.end();
            QVideoFrame frame(img);
            frame.setStartTime(framesSent * 1000000LL / FPS);
            frame.setEndTime((framesSent + 1) * 1000000LL / FPS);
            if (!input.sendVideoFrame(frame))
                return; // wait for readyToSendVideoFrame
            ++framesSent;
        }
        recorder.stop();
    };
    QObject::connect(&input, &QVideoFrameInput::readyToSendVideoFrame, &app, sendFrames);
    QObject::connect(&recorder, &QMediaRecorder::recorderStateChanged, &app,
        [&](QMediaRecorder::RecorderState s) {
            if (s == QMediaRecorder::StoppedState) {
                qInfo() << "wrote" << recorder.actualLocation().toLocalFile();
                app.exit(0);
            }
        });
    QObject::connect(&recorder, &QMediaRecorder::errorOccurred, &app,
        [&](QMediaRecorder::Error, const QString& msg) {
            qWarning() << "recorder error:" << msg;
            app.exit(1);
        });
    QTimer::singleShot(120000, &app, [&]() { qWarning("timeout"); app.exit(1); });
    recorder.record();
    QTimer::singleShot(0, &app, sendFrames);
    return app.exec();
}
