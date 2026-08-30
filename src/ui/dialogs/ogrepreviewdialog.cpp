#include "ui/dialogs/ogrepreviewdialog.h"
#include "viewport/engineviewwidget.h"
#include "viewport/enginerenderdriver.h"
#include "bridge/enginehost.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDir>
#include <QCoreApplication>
#include <QGuiApplication>

using namespace jahshaka::engine;

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

    QString error;
    if (!EngineHost::instance().start(error)) {
        mStatus->setText(tr("Engine failed to start: %1").arg(error));
        mStatus->setTextFormat(Qt::RichText);
        return;
    }
    mEngine = EngineHost::instance().engine();

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

    // The ONE render loop (EngineHost's). renderOneFrame() draws every enabled view.
    auto *driver = EngineHost::instance().driver();
    connect(driver, &EngineRenderDriver::beforeFrame, this, [this]() {
        if (mEditorScene)  mEditorScene->rotateNode(mCube,  0.012f, 0.0f, 0.005f);
        if (mEffectsScene) mEffectsScene->rotateNode(mCube2, 0.0f, 0.010f, 0.0f);
    });
    if (!driver->isRunning()) {
        driver->start(16);
        mStartedDriver = true;
    }
}

OgrePreviewDialog::~OgrePreviewDialog()
{
    // Deterministic teardown: release our views and scenes while the Engine is
    // alive. The Engine itself belongs to EngineHost and outlives this dialog.
    auto *driver = EngineHost::instance().driver();
    if (driver) disconnect(driver, nullptr, this, nullptr);
    if (driver && mStartedDriver) driver->stop();
    if (mEditorView)  mEditorView->destroyView();
    if (mEffectsView) mEffectsView->destroyView();
    if (mEngine) {
        if (mEditorScene)  mEngine->destroyScene(mEditorScene);
        if (mEffectsScene) mEngine->destroyScene(mEffectsScene);
    }
    mEditorScene = mEffectsScene = nullptr;
    mEngine.reset();
}
