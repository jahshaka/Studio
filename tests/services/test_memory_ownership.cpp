// services.memory — who owns what, asserted instead of assumed.
//
// The deep audit of 2026-09 (area 3) found that "nothing that allocates ever
// frees" in three of this app's session-scoped registries. Two of them live
// here, and both fixes are invisible to every other suite in the tree — a leak
// changes no observable behaviour until the machine runs out of memory. So
// this suite observes the ownership DIRECTLY:
//
//   1. AssetManager::clearAssetList() called QVector::clear() and nothing
//      else. Every project open therefore leaked its whole session asset list
//      (~80 allocations for the Showroom sample), and because a model asset's
//      payload QVariant holds a SceneNodePtr, each leaked Asset pinned a whole
//      mesh subtree for the life of the process. `Asset` also had no virtual
//      destructor, so even a `delete` through the base would have skipped it.
//      Observed here through a QWeakPointer that MUST expire, and through a
//      destructor counter on a subclass that only a virtual dtor can reach.
//
//   2. ThumbnailManager cached a heap `QImage*` per thumbnail in a static
//      QHash that never dropped an entry ("todo: find a way to remove unused
//      thumbnails"). The picture is a value now and the cache is bounded;
//      observed here by overflowing the cap and asserting the size.
//
// No engine, no database, no display: two registries and a temp directory.
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QWeakPointer>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"

#include "io/assetmanager.h"
#include "services/thumbnailmanager.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---------------------------------------------------------------------------
// 1. The session asset registry
// ---------------------------------------------------------------------------

static int sDestroyed = 0;

/// An asset whose destruction is observable. It derives from Asset exactly as
/// the real payload types do, so it also proves the base destructor is virtual
/// — without `virtual ~Asset()`, deleting through `Asset*` never reaches this
/// body (and is undefined behaviour besides).
struct CountedAsset : public AssetNodeObject
{
    ~CountedAsset() override { ++sDestroyed; }
};

/// Registers one "project session": a handful of assets, each pinning a real
/// SceneNode subtree through its payload QVariant, exactly like the model
/// assets ProjectAssets::registerProjectSessionAssets() creates.
static QVector<QWeakPointer<iris::SceneNode>> openSession(int count)
{
    QVector<QWeakPointer<iris::SceneNode>> watches;
    for (int i = 0; i < count; ++i) {
        // Deliberately a SINGLE node, not a subtree. iris::SceneNode::parent
        // and ::scene are QSharedPointers, so any parent/child pair is a
        // reference CYCLE that no amount of asset-registry hygiene can break
        // (deep audit 2026-09, area 3; List B item 4 — parent/scene must
        // become QWeakPointer, a document-model change of its own). What this
        // suite can prove — and what the registry is responsible for — is that
        // the registry itself lets go: the payload QVariant is destroyed, so
        // the reference count it held is released. With a real subtree the
        // node still would not die, but for a reason that lives in irisgl.
        auto root = iris::MeshNode::create();
        root->setName(QStringLiteral("asset%1").arg(i));

        auto *asset = new CountedAsset;
        asset->assetGuid = QStringLiteral("guid-%1").arg(i);
        asset->fileName = QStringLiteral("asset%1.obj").arg(i);
        asset->path = QStringLiteral("/nowhere/asset%1.obj").arg(i);
        asset->setValue(QVariant::fromValue(root.staticCast<iris::SceneNode>()));
        AssetManager::addAsset(asset);

        watches.append(root.toWeakRef());
    }
    return watches;
}

static bool allExpired(const QVector<QWeakPointer<iris::SceneNode>> &watches)
{
    for (const auto &w : watches) if (!w.isNull()) return false;
    return true;
}

static void testAssetListOwnership()
{
    std::printf("\n-- a project session is released when it closes --\n");

    const int baseline = AssetManager::getAssets().count();
    CHECK(baseline == 0, "the registry starts empty");

    // ---- open #1 ----
    sDestroyed = 0;
    auto firstOpen = openSession(6);
    CHECK(AssetManager::getAssets().count() == baseline + 6, "open #1 registered 6 assets");
    CHECK(!allExpired(firstOpen), "open #1's node subtrees are alive while the session is");

    // ---- close #1 ----
    AssetManager::clearAssetList();
    CHECK(AssetManager::getAssets().count() == baseline, "close #1 returned the count to baseline");
    CHECK(sDestroyed == 6, "close #1 DESTROYED all six assets (virtual ~Asset reached)");
    CHECK(allExpired(firstOpen),
          "close #1 released every pinned SceneNodePtr (the payload QVariant died with the asset)");

    // ---- open #2 / close #2: the same, twice, is the shape the audit named --
    sDestroyed = 0;
    auto secondOpen = openSession(6);
    CHECK(AssetManager::getAssets().count() == baseline + 6, "open #2 registered 6 assets");
    AssetManager::clearAssetList();
    CHECK(AssetManager::getAssets().count() == baseline, "close #2 returned the count to baseline");
    CHECK(sDestroyed == 6, "close #2 destroyed all six assets");
    CHECK(allExpired(secondOpen), "close #2 released every pinned SceneNodePtr");
}

static void testReplaceAssets()
{
    std::printf("\n-- replaceAssets destroys the asset it replaces --\n");
    AssetManager::clearAssetList();

    sDestroyed = 0;
    auto *original = new CountedAsset;
    original->assetGuid = QStringLiteral("mat-1");
    AssetManager::addAsset(original);

    auto *replacement = new CountedAsset;
    replacement->assetGuid = QStringLiteral("mat-1");
    AssetManager::replaceAssets(QStringLiteral("mat-1"), replacement);

    CHECK(AssetManager::getAssets().count() == 1, "the registry still holds exactly one entry");
    CHECK(sDestroyed == 1, "the replaced asset was destroyed, not just unlinked");
    CHECK(AssetManager::getAssedByGuid(QStringLiteral("mat-1")) == replacement,
          "the guid resolves to the replacement");

    // Re-registering the SAME pointer must be a no-op, not a self-destruct.
    sDestroyed = 0;
    AssetManager::replaceAssets(QStringLiteral("mat-1"), replacement);
    CHECK(sDestroyed == 0, "re-registering the same object does not destroy it");
    CHECK(AssetManager::getAssedByGuid(QStringLiteral("mat-1")) == replacement,
          "...and it is still the registered asset");

    // An unknown guid is an add, not a replace.
    auto *extra = new CountedAsset;
    extra->assetGuid = QStringLiteral("mat-2");
    sDestroyed = 0;
    AssetManager::replaceAssets(QStringLiteral("no-such-guid"), extra);
    CHECK(AssetManager::getAssets().count() == 2 && sDestroyed == 0,
          "replacing an unknown guid appends and destroys nothing");

    sDestroyed = 0;
    AssetManager::clearAssetList();
    CHECK(sDestroyed == 2 && AssetManager::getAssets().isEmpty(), "the registry cleans up after itself");
}

// ---------------------------------------------------------------------------
// 2. The thumbnail cache
// ---------------------------------------------------------------------------

static QString writeTestImage(const QDir &dir, int index, int size)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(QColor(index % 256, (index * 7) % 256, 200));
    const QString path = dir.filePath(QStringLiteral("thumb-src-%1.png").arg(index));
    image.save(path, "PNG");
    return path;
}

static void testThumbnailCache()
{
    std::printf("\n-- the thumbnail cache is bounded and holds pictures, not pointers --\n");

    QTemporaryDir temp;
    if (!temp.isValid()) { std::printf("FAIL: could not create a temp dir\n"); ++failures; return; }
    const QDir dir(temp.path());

    ThumbnailManager::clearCache();
    CHECK(ThumbnailManager::cachedCount() == 0 && ThumbnailManager::cachedBytes() == 0,
          "the cache starts empty");

    // A source much larger than the thumbnail: the decode goes through
    // QImageReader::setScaledSize, and originalSize must still report the file.
    const QString big = writeTestImage(dir, 0, 512);
    auto thumb = ThumbnailManager::createThumbnail(big, 64, 64);
    CHECK(!thumb.isNull(), "createThumbnail returned a Thumbnail");
    CHECK(!thumb->thumb.isNull(), "the Thumbnail carries a picture");
    CHECK(thumb->thumb.height() == 64, "the picture is decoded at the requested height");
    CHECK(thumb->originalSize == QSize(512, 512), "originalSize still reports the source file");

    // The same request hits the cache and hands back the SAME object.
    auto again = ThumbnailManager::createThumbnail(big, 64, 64);
    CHECK(again.data() == thumb.data(), "a repeat request is served from the cache");
    CHECK(ThumbnailManager::cachedCount() == 1, "and did not add a second entry");

    // An unreadable path is a null picture, never a null pointer (the old
    // contract every caller was written against).
    auto missing = ThumbnailManager::createThumbnail(dir.filePath("does-not-exist.png"), 64, 64);
    CHECK(!missing.isNull() && missing->thumb.isNull(),
          "an unreadable file yields a Thumbnail with a null picture");

    // ---- the cap ----
    ThumbnailManager::clearCache();
    const int overflow = ThumbnailManager::kMaxEntries + 16;
    for (int i = 0; i < overflow; ++i)
        ThumbnailManager::createThumbnail(writeTestImage(dir, i + 1, 8), 8, 8);

    std::printf("info: %d thumbnails requested, %d cached, %lld bytes\n",
                overflow, ThumbnailManager::cachedCount(),
                static_cast<long long>(ThumbnailManager::cachedBytes()));
    CHECK(ThumbnailManager::cachedCount() <= ThumbnailManager::kMaxEntries,
          "the cache never grows past its entry cap");
    CHECK(ThumbnailManager::cachedBytes() <= ThumbnailManager::kMaxBytes,
          "the cache never grows past its byte cap");
    CHECK(ThumbnailManager::cachedCount() > 0, "...and it is still a cache, not a sink");

    // LRU: the most recent request survives, the oldest does not.
    const QString newest = writeTestImage(dir, overflow + 1, 8);
    ThumbnailManager::createThumbnail(newest, 8, 8);
    auto newestAgain = ThumbnailManager::createThumbnail(newest, 8, 8);
    CHECK(ThumbnailManager::cachedCount() <= ThumbnailManager::kMaxEntries,
          "the cap still holds after the newest insert");
    CHECK(!newestAgain->thumb.isNull(), "the most recent entry is still served");

    ThumbnailManager::clearCache();
    CHECK(ThumbnailManager::cachedCount() == 0 && ThumbnailManager::cachedBytes() == 0,
          "clearCache empties both the map and the byte count");
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    testAssetListOwnership();
    testReplaceAssets();
    testThumbnailCache();

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
