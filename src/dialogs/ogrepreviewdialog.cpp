#include "ogrepreviewdialog.h"
#include "../widgets/engineviewwidget.h"
#include "../widgets/enginerenderdriver.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDir>
#include <QCoreApplication>
#include <QGuiApplication>

using namespace jahshaka::engine;

EngineConfig OgrePreviewDialog::resolveConfig()
{
    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;

    const QByteArray envPlugins = qgetenv("JAHSHAKA_OGRE_PLUGINS");
    const QByteArray envMedia   = qgetenv("JAHSHAKA_OGRE_MEDIA");
    const QString appMedia = QCoreApplication::applicationDirPath() + QStringLiteral("/media/");

    if (!envPlugins.isEmpty())                      cfg.pluginDir = envPlugins.toStdString();
    else                                            cfg.pluginDir = JAHSHAKA_OGRE_PLUGIN_DIR_DEFAULT;

    if (!envMedia.isEmpty())                        cfg.hlmsMediaDir = envMedia.toStdString();
    else if (QDir(appMedia + "Hlms/Pbs").exists())  cfg.hlmsMediaDir = appMedia.toStdString();
    else                                            cfg.hlmsMediaDir = JAHSHAKA_OGRE_MEDIA_DIR_DEFAULT;

    cfg.logFile = "jahshaka-ogre.log";
    return cfg;
}

OgrePreviewDialog::OgrePreviewDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Engine preview — Ogre-Next"));
    resize(1000, 480);

    auto *outer = new QVBoxLayout(this);
    mStatus = new QLabel(tr("starting engine..."), this);
    outer->addWidget(mStatus);

    auto *row = new QHBoxLayout();
    outer->addLayout(row, 1);

    // Two INDEPENDENT views, each with its own scene — the shape the modules take:
    // editor and player share a scene; effects and assets share nothing.
    auto *colA = new QVBoxLayout();
    colA->addWidget(new QLabel(tr("Editor / Player — shared scene"), this));
    mEditorView = new EngineViewWidget(this);
    colA->addWidget(mEditorView, 1);

    auto *colB = new QVBoxLayout();
    colB->addWidget(new QLabel(tr("Effects — its own scene"), this));
    mEffectsView = new EngineViewWidget(this);
    colB->addWidget(mEffectsView, 1);

    row->addLayout(colA, 1);
    row->addLayout(colB, 1);

    show();
    // Native window ids must exist before the engine can bind to them.
    QCoreApplication::processEvents();

    // Ogre has no Wayland backend: its Vulkan path uses VK_KHR_xcb_surface and needs a
    // real X11 window. Jahshaka forces QT_QPA_PLATFORM=wayland (main.cpp) because the OLD
    // Qt-GL viewport only renders there. The two cannot coexist yet, so refuse clearly
    // rather than hand Ogre a Wayland handle and crash.
    const QString platform = QGuiApplication::platformName();
    if (platform != QLatin1String("xcb")) {
        mStatus->setText(
            tr("<b>Requires the xcb platform.</b> Running on '%1'.<br>"
               "Ogre-Next has no Wayland backend — relaunch with:<br>"
               "<tt>QT_QPA_PLATFORM=xcb ./Jahshaka</tt><br><br>"
               "Note: the old editor viewport does not render on xcb; that is the "
               "transition this migration removes.").arg(platform));
        mStatus->setTextFormat(Qt::RichText);
        return;
    }

    EngineConfig cfg = resolveConfig();
    // Hand the engine OUR X connection. Opening a second connection to the same
    // windows causes flicker and lets other windows' content bleed into the viewport.
    if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>())
        cfg.display = reinterpret_cast<NativeDisplayHandle>(x11->display());
    if (!cfg.display) {
        mStatus->setText(tr("Could not obtain the X11 display connection from Qt."));
        return;
    }

    std::string error;
    mEngine = Engine::create(cfg, error);
    if (!mEngine) {
        mStatus->setText(tr("Engine failed to start: %1").arg(QString::fromStdString(error)));
        return;
    }

    // ORDER: views (windows) first, then scenes — the engine's material and buffer
    // systems only start once a render window exists.
    mEditorView->createView(mEngine, "editor",  Colour(0.10f, 0.11f, 0.14f));
    mEffectsView->createView(mEngine, "effects", Colour(0.16f, 0.12f, 0.10f));

    mEditorScene = mEngine->createScene("editor");
    if (!mEditorScene) {
        mStatus->setText(tr("Scene creation failed: %1")
                             .arg(QString::fromStdString(mEngine->lastError())));
        return;
    }
    mEditorScene->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));
    mEditorScene->addDirectionalLight(Vec3(-0.55f, -0.7f, -0.45f), 3.14159f);
    mCube = mEditorScene->addTestCube(Colour(0.85f, 0.35f, 0.15f), 0.85f, 0.25f);

    mEffectsScene = mEngine->createScene("effects");
    if (mEffectsScene) {
        mEffectsScene->setAmbient(Colour(0.20f, 0.22f, 0.30f), Colour(0.10f, 0.12f, 0.16f));
        mEffectsScene->addDirectionalLight(Vec3(0.4f, -0.8f, 0.35f), 3.14159f);
        mCube2 = mEffectsScene->addTestCube(Colour(0.20f, 0.55f, 0.85f), 0.10f, 0.55f);
    }

    if (auto *v = mEditorView->view()) {
        v->setScene(mEditorScene);
        v->setCameraPosition(Vec3(2.6f, 1.9f, 3.4f));
        v->lookAt(Vec3(0.0f, 0.0f, 0.0f));
    }
    if (auto *v = mEffectsView->view()) {
        v->setScene(mEffectsScene);
        v->setCameraPosition(Vec3(-3.0f, 2.6f, -2.2f));
        v->lookAt(Vec3(0.0f, 0.0f, 0.0f));
    }

    mStatus->setText(tr("Backend: %1   —   2 windows, 2 independent scenes")
                         .arg(QString::fromStdString(mEngine->backendName())));

    // The ONE render loop. renderOneFrame() draws every enabled view.
    mDriver = new EngineRenderDriver(mEngine.get(), this);
    connect(mDriver, &EngineRenderDriver::beforeFrame, this, [this]() {
        if (mEditorScene)  mEditorScene->rotateNode(mCube,  0.012f, 0.0f, 0.005f);
        if (mEffectsScene) mEffectsScene->rotateNode(mCube2, 0.0f, 0.010f, 0.0f);
    });
    mDriver->start(16);
}

OgrePreviewDialog::~OgrePreviewDialog()
{
    // Deterministic teardown: stop the loop, release the views while the Engine is
    // alive, then the Engine (which takes the scenes with it).
    if (mDriver) mDriver->stop();
    if (mEditorView)  mEditorView->destroyView();
    if (mEffectsView) mEffectsView->destroyView();
    mEngine.reset();
}
