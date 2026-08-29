#ifndef OGREPREVIEWDIALOG_H
#define OGREPREVIEWDIALOG_H

// Preview window for the new engine backend.
//
// Temporary scaffolding: proves the engine boundary works inside the real
// application, and is the harness the editor viewport migration is built on.
// Includes only the engine abstraction — never Ogre.
#include <QDialog>
#include <memory>
#include "jahshaka/engine/Engine.h"

class EngineViewWidget;
class QLabel;

class OgrePreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OgrePreviewDialog(QWidget *parent = nullptr);
    ~OgrePreviewDialog() override;

private:
    std::unique_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::Scene *mEditorScene  = nullptr;
    jahshaka::engine::Scene *mEffectsScene = nullptr;
    jahshaka::engine::NodeId mCube  = 0;
    jahshaka::engine::NodeId mCube2 = 0;
    EngineViewWidget *mEditorView  = nullptr;
    EngineViewWidget *mEffectsView = nullptr;
    QLabel *mStatus = nullptr;
};

#endif // OGREPREVIEWDIALOG_H
