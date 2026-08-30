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

#include <cmath>

using namespace jahshaka::engine;

// Local stand-ins for the convenience verbs the engine boundary used to carry
// (Scene::addTestCube/addDirectionalLight, View::setCameraPosition/lookAt,
// Scene::rotateNode). They speak only the real verbs, so this dialog now
// exercises the same code path the editor viewport does.
namespace {

/// The unit cube the deleted addTestCube built: 24 vertices, positions and
/// per-face normals, no uvs.
MeshData unitCubeMesh()
{
    MeshData d;
    const float h = 0.5f;
    const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const float fv[6][4][3] = {
        {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}},
        {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}},
        {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}},
        {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
        {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}},
        {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}} };
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            d.positions.insert(d.positions.end(), { fv[f][v][0], fv[f][v][1], fv[f][v][2] });
            d.normals.insert(d.normals.end(), { fn[f][0], fn[f][1], fn[f][2] });
        }
        const unsigned b = unsigned(f * 4);
        d.indices.insert(d.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return d;
}

NodeId addCube(Scene *s, const Colour &albedo, float metalness, float roughness)
{
    if (!s) return 0;
    const NodeId node = s->createNode();
    if (!node) return 0;
    const MeshId mesh = s->createMesh(unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = metalness;
    p.roughness = roughness;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    return node;
}

/// Rotates the world -Y axis onto `direction` — lights shine down their node's -Y.
Quat aimMinusYAlong(const Vec3 &direction)
{
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                direction.z * direction.z);
    if (len <= 0.0f) return Quat();
    const Vec3 d{ direction.x / len, direction.y / len, direction.z / len };
    const Vec3 from{ 0.0f, -1.0f, 0.0f };
    const Vec3 cross{ from.y * d.z - from.z * d.y,
                      from.z * d.x - from.x * d.z,
                      from.x * d.y - from.y * d.x };
    const float dot = from.x * d.x + from.y * d.y + from.z * d.z;
    if (dot < -0.999999f) return Quat(0.0f, 0.0f, 1.0f, 0.0f);      // 180 degrees about Z
    const float w = 1.0f + dot;
    const float n = std::sqrt(w * w + cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
    return Quat(cross.x / n, cross.y / n, cross.z / n, w / n);
}

/// addDirectionalLight(direction, power): a node aimed down `direction` carrying
/// a directional light. setLight applies powerScale = intensity * pi, so the raw
/// power the old verb pushed becomes intensity = power / pi.
NodeId addDirectionalLight(Scene *s, const Vec3 &direction, float power)
{
    if (!s) return 0;
    const NodeId node = s->createNode();
    if (!node) return 0;
    s->setNodeTransform(node, Vec3(0, 0, 0), aimMinusYAlong(direction), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Directional;
    l.colour = Colour(1.0f, 1.0f, 1.0f);
    l.intensity = power / 3.14159265358979323846f;
    if (!s->setLight(node, l)) return 0;
    return node;
}

Quat mul(const Quat &a, const Quat &b)
{
    return Quat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

Quat axisAngle(const Vec3 &axis, float radians)
{
    const float s = std::sin(radians * 0.5f);
    return Quat(axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f));
}

/// The deleted rotateNode: LOCAL yaw (Y), then pitch (X), then roll (Z),
/// composed onto the node's current orientation.
Quat spin(const Quat &current, float yaw, float pitch, float roll)
{
    Quat q = mul(current, axisAngle(Vec3(0, 1, 0), yaw));
    q = mul(q, axisAngle(Vec3(1, 0, 0), pitch));
    q = mul(q, axisAngle(Vec3(0, 0, 1), roll));
    return q;
}

/// setCameraPosition + lookAt(target), +Y up — camera looks down its local -Z.
void cameraLookAt(View *v, const Vec3 &pos, const Vec3 &target)
{
    if (!v) return;
    const Vec3 f0{ target.x - pos.x, target.y - pos.y, target.z - pos.z };
    const float fl = std::sqrt(f0.x * f0.x + f0.y * f0.y + f0.z * f0.z);
    if (fl <= 0.0f) return;
    const Vec3 f{ f0.x / fl, f0.y / fl, f0.z / fl };
    const Vec3 upW{ 0.0f, 1.0f, 0.0f };
    Vec3 r{ f.y * upW.z - f.z * upW.y, f.z * upW.x - f.x * upW.z, f.x * upW.y - f.y * upW.x };
    const float rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    r = Vec3(r.x / rl, r.y / rl, r.z / rl);
    const Vec3 u{ r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
    const float m00 = r.x, m01 = u.x, m02 = -f.x;
    const float m10 = r.y, m11 = u.y, m12 = -f.y;
    const float m20 = r.z, m21 = u.z, m22 = -f.z;
    const float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.0f) {
        const float sq = std::sqrt(tr + 1.0f) * 2.0f;
        q = Quat((m21 - m12) / sq, (m02 - m20) / sq, (m10 - m01) / sq, 0.25f * sq);
    } else if (m00 > m11 && m00 > m22) {
        const float sq = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = Quat(0.25f * sq, (m01 + m10) / sq, (m02 + m20) / sq, (m21 - m12) / sq);
    } else if (m11 > m22) {
        const float sq = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = Quat((m01 + m10) / sq, 0.25f * sq, (m12 + m21) / sq, (m02 - m20) / sq);
    } else {
        const float sq = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = Quat((m02 + m20) / sq, (m12 + m21) / sq, 0.25f * sq, (m10 - m01) / sq);
    }
    CameraDesc c;
    c.position = pos;
    c.orientation = q;
    v->setCamera(c);
}

}  // namespace

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
    addDirectionalLight(mEditorScene, Vec3(-0.55f, -0.7f, -0.45f), 3.14159f);
    mCube = addCube(mEditorScene, Colour(0.85f, 0.35f, 0.15f), 0.85f, 0.25f);

    mEffectsScene = mEngine->createScene("effects");
    if (mEffectsScene) {
        mEffectsScene->setAmbient(Colour(0.20f, 0.22f, 0.30f), Colour(0.10f, 0.12f, 0.16f));
        addDirectionalLight(mEffectsScene, Vec3(0.4f, -0.8f, 0.35f), 3.14159f);
        mCube2 = addCube(mEffectsScene, Colour(0.20f, 0.55f, 0.85f), 0.10f, 0.55f);
    }

    if (auto *v = mEditorView->view()) {
        v->setScene(mEditorScene);
        cameraLookAt(v, Vec3(2.6f, 1.9f, 3.4f), Vec3(0.0f, 0.0f, 0.0f));
    }
    if (auto *v = mEffectsView->view()) {
        v->setScene(mEffectsScene);
        cameraLookAt(v, Vec3(-3.0f, 2.6f, -2.2f), Vec3(0.0f, 0.0f, 0.0f));
    }

    mStatus->setText(tr("Backend: %1   —   2 windows, 2 independent scenes")
                         .arg(QStringLiteral("Vulkan (Ogre-Next)")));

    // The ONE render loop (EngineHost's). renderOneFrame() draws every enabled view.
    auto *driver = EngineHost::instance().driver();
    connect(driver, &EngineRenderDriver::beforeFrame, this, [this]() {
        if (mEditorScene) {
            mCubeRot = spin(mCubeRot, 0.012f, 0.0f, 0.005f);
            mEditorScene->setNodeTransform(mCube, Vec3(0, 0, 0), mCubeRot, Vec3(1, 1, 1));
        }
        if (mEffectsScene) {
            mCube2Rot = spin(mCube2Rot, 0.0f, 0.010f, 0.0f);
            mEffectsScene->setNodeTransform(mCube2, Vec3(0, 0, 0), mCube2Rot, Vec3(1, 1, 1));
        }
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
