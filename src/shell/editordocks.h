/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORDOCKS_H
#define EDITORDOCKS_H

// EditorDocks — THE EDITOR'S PANELS, AS ONE PART OF THE SHELL (D10-SHELL-MODULES).
//
// The six docks of the editor page's nested QMainWindow — Hierarchy, Properties
// (with its World | Selection tab strip), Presets, and the bottom area's one tab
// group of Assets, Timeline and Console — with everything that keeps them
// honest: which are open (`widgetStates`, the session's record), which bottom
// tab is in front, the column widths and the Presets line, the saved layout,
// immersive fullscreen's hide and restore, the Toggle Widgets dialog, and the
// readings the verbs report (app.docks, editor.tray, editor.panel, the
// properties column's rows and filter).
//
// ---- the editor's BOTTOM AREA (owner, 2026-09-14, lane SPACE-2) --------
// ONE tab bar along the bottom of the editor: "Assets" (the asset browser),
// "Timeline" (the keyframe panel) and "Console" (the script console) when it is
// turned on. All three are DOCKS of the editor's nested window, tabified into
// one group whose tab bar sits at the TOP of the area (setTabPosition(North)) —
// the bar used to be Qt's default SOUTH one, a 20 px strip along the very
// bottom edge of the window under a tray that carried its own tab bar at the
// top, which is how the Timeline came to read as "gone" (owner report,
// 2026-09-14). Ctrl+` and the `editor.tray` / `editor.trayState` verbs go
// through these — the key and the verb are the same code path, which is what
// lets a suite assert what the key did.

#include <QByteArray>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QWeakPointer>

#include "irisgl/irisglfwd.h"
#include "data/project.h"

#include <functional>

class AnimationWidget;
class AssetMaterialPanel;
class AssetModelPanel;
class AssetWidget;
class Database;
class IEditorViewport;
class MainWindow;
class Project;
class PropertiesTabStrip;
class QAction;
class QDockWidget;
class QListWidgetItem;
class QMainWindow;
class QTabWidget;
class QToolBar;
class QWidget;
class SceneHierarchyWidget;
class SceneNodePropertiesWidget;
class ScriptConsole;
class SettingsManager;
struct StudioServices;

// The editor's panels, in `widgetStates` order. CONSOLE is APPENDED (lane
// SPACE-2, 2026-09-14): the script console is a dock of the bottom area again —
// the third tab beside Assets and the Timeline — so it needs the same record
// every other panel has (the space switch hides it with the rest, its title-bar
// X closes it for good, a restart brings back the tab the user left open). It
// is deliberately NOT one of the Toggle Widgets dialog's buttons: Ctrl+` is the
// console's switch, and "Restore All" restoring a console nobody asked for is
// not what that button means.
enum class Widget
{
	HIERARCHY,
	PROPERTIES,
	ASSETS,
	TIMELINE,
	PRESETS,
	CONSOLE
};

class EditorDocks : public QObject
{
    Q_OBJECT
public:
    explicit EditorDocks(QObject *parent = nullptr);

    /// What building the docks needs.
    struct Deps {
        MainWindow *shell = nullptr;      ///< the panels' window, and the slots they signal
        QWidget *window = nullptr;        ///< dialog parent and dockReport's coordinate space
        QMainWindow *viewPort = nullptr;  ///< the editor page's nested window
        IEditorViewport *viewport = nullptr;
        Database *db = nullptr;
        StudioServices *services = nullptr;
        Project *project = nullptr;
        SettingsManager *settings = nullptr;
        /// The World blade's Show Grid and Ground Plane rows are second faces
        /// of the View Options menu's two actions.
        QAction *gridAction = nullptr;
        QAction *groundPlaneAction = nullptr;
        /// The editor page is on screen and not in immersive fullscreen.
        std::function<bool()> editorOnScreen;
        /// The editor is the active space (fullscreen hides the chrome only there).
        std::function<bool()> editorActive;
    };
    /// Builds the six docks, the default layout and the USER's restored one.
    void build(const Deps &deps);
    /// The panels follow the services: the edits' refresh notifications (the
    /// outliner's rows, the transform rows, the tray, the material blade), a
    /// paste's library import, and an undo's repaint of the properties column.
    void followServices(StudioServices *svc);
    /// The console widget (it needs the script engine, made after the docks).
    void setConsole(ScriptConsole *console);
    /// The editor toolbar, which immersive fullscreen hides with the docks.
    void setToolbar(QToolBar *bar);

    // ---- the panels ------------------------------------------------------
    SceneHierarchyWidget *hierarchy() const { return sceneHierarchyWidget; }
    SceneNodePropertiesWidget *properties() const { return sceneNodePropertiesWidget; }
    AssetWidget *assetTray() const { return assetWidget; }
    AssetMaterialPanel *materialTray() const { return assetMaterialPanel; }
    AnimationWidget *timeline() const { return animationWidget; }
    QDockWidget *hierarchyDock() const { return sceneHierarchyDock; }
    QDockWidget *propertiesDock() const { return sceneNodePropertiesDock; }

    // ---- visibility, layout, persistence ----------------------------------
    /// Shows or hides the editor's docks for the page that is on screen: the
    /// editor shows the ones `widgetStates` says are open, every other page
    /// shows none. The ONE place dock visibility follows a space.
    void applyVisibility();
    /// The columns' default widths, once per session (after the page is up).
    void applyColumnWidthsOnce();
    /// THE PRESETS LINE: the right column's Presets panel starts on the SAME
    /// horizontal line as the bottom Tray (owner 2026-09-12).
    void alignPresetsWithTray(int retries = 3);
    /// The editor's layout snapshot (a no-op while the docks are hidden).
    void captureLayout();
    /// The snapshot into the settings (the window close).
    void storeLayout();
    void hideForFullscreen();
    void restoreAfterFullscreen();
    /// The Toggle Widgets dialog (the toolbar's viewDocks action).
    void openToggleDialog();

    // ---- the bottom area -------------------------------------------------
    QString trayTab() const;
    QStringList trayTabs() const;
    bool setTrayTab(const QString &tab, bool focusConsoleInput = true);
    bool isConsoleTabVisible() const;
    void setConsoleTabVisible(bool visible, bool focusInput = true);
    bool isConsoleInputFocused() const;
    bool isTrayVisible() const;
    bool setTrayHeight(int height);
    QDockWidget *bottomFrontDock() const;
    int bottomAreaTop() const;
    /// Ctrl+` : show + focus the Console tab, or hide it when it is already the
    /// tab in front.
    void toggleScriptConsole();

    // ---- panels by name ---------------------------------------------------
    QDockWidget *panelDock(const QString &name) const;
    bool setPanelOpen(const QString &name, bool open);
    bool isPanelOpen(const QString &name) const;
    bool raisePanel(const QString &name);
    /// Whether `dock` is the tab in FRONT of its group (and not closed).
    static bool isFrontTab(const QDockWidget *dock);
    /// THE EDITOR'S DOCKS, MEASURED (app.docks).
    QVariantList dockReport() const;

    // ---- the properties column ---------------------------------------------
    QString propertiesTab() const;
    bool setPropertiesTab(const QString &name);
    bool isPropertiesTab(const QString &tabName) const;
    QString propertiesFilter(const QString &tabName = QString()) const;
    bool setPropertiesFilter(const QString &tabName, const QString &text);
    QPair<int, int> propertiesFilterCounts(const QString &tabName = QString()) const;
    QVariantList propertyRows(const QString &tabName = QString()) const;
    QVariantMap propertyRow(const QString &tabName, const QString &key, bool drive,
                            const QVariant &value, QString *error);
    QVariantMap propertiesStats() const;
    void togglePropertiesTab();
    void focusPropertiesFilter();
    /// Re-reads the column from the document (the edit gate's repaint).
    void refreshPropertiesFromDocument();

    /// THE PANELS FOLLOW THE SELECTION: the primary into the viewport, the
    /// properties column, the outliner and the timeline (SelectionService::
    /// selectionChanged), the SET into the viewport and the outliner
    /// (selectionSetChanged). Each consumer is charged to editor.selectionCost.
    void showSelection(iris::SceneNodePtr sceneNode);
    void showSelectionSet(const QList<iris::SceneNodePtr> &nodes);

    /// The outliner's Export: the node through the save dialog and the bundle
    /// writer, behind its progress dialog (node.exportArchive's own stage).
    void exportNode(const iris::SceneNodePtr &node, ModelTypes modelType);

    /// A favourited tray item goes to the Presets panel of its kind.
    void favoriteItem(QListWidgetItem *item);

    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QVector<QPair<QString, QDockWidget *>> bottomAreaTabs() const;
    void raiseBottomFrontTab();
    /// THE LAUNCH TAB (owner review R7, 2026-09-18; the rule of 2026-09-15).
    ///
    /// EVERY SESSION OPENS ON ASSETS. Which bottom tab is in front is SESSION
    /// state, not a preference: inside a session it follows the user and
    /// survives space switches, fullscreen and the console's visit — but a
    /// LAUNCH always starts on the asset browser, whatever the saved DockState
    /// blob remembers, because that blob records where the last session HAPPENED
    /// to stop (often the Timeline, or the Console after a Ctrl+`).
    ///
    /// It is a function because the blob is restored TWICE — once in build(),
    /// and again from applyColumnWidthsOnce at the window's real size (the
    /// columns come back too narrow otherwise, smoke S1) — and the second
    /// restore silently re-applied the blob's front tab. Both restores are
    /// followed by this call. It sets `bottomFrontTab` as well as raising the
    /// dock: the very next thing applyVisibility does is READ the current front
    /// tab into that field, so a raise alone would be read straight back out.
    void raiseLaunchBottomTab();

    MainWindow *mShell = nullptr;
    QWidget *mWindow = nullptr;
    QMainWindow *viewPort = nullptr;
    IEditorViewport *sceneView = nullptr;
    Database *db = nullptr;
    StudioServices *services = nullptr;
    Project *project = nullptr;
    SettingsManager *settings = nullptr;
    std::function<bool()> mEditorOnScreen;
    std::function<bool()> mEditorActive;
    ScriptConsole *mConsole = nullptr;
    QToolBar *mToolbar = nullptr;

    QDockWidget *sceneHierarchyDock = nullptr;
    SceneHierarchyWidget *sceneHierarchyWidget = nullptr;
    QDockWidget *sceneNodePropertiesDock = nullptr;
    SceneNodePropertiesWidget *sceneNodePropertiesWidget = nullptr;
    /// The right column's World | Selection tab bar (above the scroll area).
    PropertiesTabStrip *propertiesTabStrip = nullptr;
    QDockWidget *presetsDock = nullptr;
    QTabWidget *presetsTabWidget = nullptr;
    AssetModelPanel *assetModelPanel = nullptr;
    AssetMaterialPanel *assetMaterialPanel = nullptr;
    /// The bottom area's three docks — ONE tab group, one tab bar (lane
    /// SPACE-2). `assetDock` holds the asset browser directly, `animationDock`
    /// the Timeline, `scriptConsoleDock` the script console, which is hidden
    /// until Ctrl+` (or editor.tray) asks for it — a hidden dock has no tab, so
    /// "the Console tab is in the bar" and "the console dock is open" are the
    /// same statement.
    QDockWidget *assetDock = nullptr;
    AssetWidget *assetWidget = nullptr;
    QDockWidget *animationDock = nullptr;
    AnimationWidget *animationWidget = nullptr;
    QDockWidget *scriptConsoleDock = nullptr;

    QVector<bool> widgetStates;   // use the order in the enum
    /// The tab that was in front the last time the bottom area was on screen —
    /// what a space round trip puts back (lane SPACE-2).
    QString bottomFrontTab = QStringLiteral("assets");
    /// The tab Ctrl+` interrupted — where the bottom area goes when the console
    /// tab is taken away again.
    QString bottomReturnTab = QStringLiteral("assets");
    bool presetsAlignQueued = false;
    /// Whether the columns' default widths have been applied — after they
    /// have, a user's drag wins.
    bool columnsSized = false;
    /// True when the nested `viewPort` QMainWindow's dock layout came back from
    /// settings — the once-per-session default column width then stands down
    /// (shell/dockstate.h).
    bool restoredViewportDocks = false;
    /// THE EDITOR'S DOCKS, AS THE EDITOR LAST HAD THEM (lane SPACE-1). Every
    /// space but the editor hides them, so the layout live at exit is the
    /// layout of whatever page the user quit from — an editor with no panels,
    /// stored as the editor's own. This is the last one the EDITOR had, taken
    /// on the way out of that space (and before immersive fullscreen hides the
    /// chrome), and it is what the window close writes.
    QByteArray editorDockState;
    /// The primary the selection fan-out last applied — for the honest "did
    /// the selection really change" count behind `editor.selectionCost()`.
    /// Weak: it is an identity, never dereferenced, and a deleted node must not
    /// be kept alive (or confused with a new one at the same address).
    QWeakPointer<iris::SceneNode> lastAppliedSelection;
    /// What the editor's chrome looked like before immersive fullscreen hid it.
    QVector<bool> preFullscreenWidgets;
};

#endif // EDITORDOCKS_H
