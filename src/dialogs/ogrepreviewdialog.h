#ifndef OGREPREVIEWDIALOG_H
#define OGREPREVIEWDIALOG_H

// Preview window for the new engine backend.
//
// Temporary scaffolding: proves the engine boundary works inside the real
// application, and is the harness the editor viewport migration is built on.
// Includes only the engine abstraction — never Ogre.
//
// Owns the Engine (one per process — see Engine.h) and the single render loop.
#include <QDialog>
#include <memory>
#include "jahshaka/engine/Engine.h"

class EngineViewWidget;
class EngineRenderDriver;
class QLabel;

class OgrePreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OgrePreviewDialog(QWidget *parent = nullptr);
    ~OgrePreviewDialog() override;

    /// Where the engine's plugins and Hlms templates are, resolved at runtime:
    /// env JAHSHAKA_OGRE_PLUGINS / JAHSHAKA_OGRE_MEDIA, then <exe dir>/media,
    /// then the build-machine defaults the engine library was configured with.
    static jahshaka::engine::EngineConfig resolveConfig();

private:
    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
    jahshaka::engine::Scene *mEditorScene  = nullptr;
    jahshaka::engine::Scene *mEffectsScene = nullptr;
    jahshaka::engine::NodeId mCube  = 0;
    jahshaka::engine::NodeId mCube2 = 0;
    EngineViewWidget *mEditorView  = nullptr;
    EngineViewWidget *mEffectsView = nullptr;
    QLabel *mStatus = nullptr;
};

#endif // OGREPREVIEWDIALOG_H
