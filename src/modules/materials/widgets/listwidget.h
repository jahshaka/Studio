#pragma once
#include <functional>
#include <QListWidget>
#include <QSize>
#include <QVariantAnimation>

struct shaderInfo {
	QString GUID;
	QString name;
	/// WHICH DRAWER THIS MATERIAL WAS OPENED FROM, and therefore whose
	/// version is being edited (MATERIAL_BUNDLE_SPEC 12 Q2, the four-drawer
	/// rule of OWNER_REVIEW 9). SCOPE IS AN ORIGIN, NOT A LOOKUP: it used to
	/// be inferred from "does the open project pin this guid?", which meant
	/// that while a project held a material there was NO WAY to open or edit
	/// the library original, and the same guid meant two different things in
	/// one window. A Custom tile is the LIBRARY's copy; a Projects tile is
	/// the project's.
	enum class Origin { Library, Project };
	Origin origin = Origin::Library;
};

class ListWidget : public QListWidget
{

    Q_OBJECT
public:
	ListWidget();
	~ListWidget();

	void displayAllContents();
	bool isResizable = false;
	void dropEvent(QDropEvent *event) override;
    bool shaderContextMenuAllowed = false;
	bool addToProjectMenuAllowed = false;
	/// The PRESETS drawer's one gesture (R18). A shipped preset is read-only
	/// — it has no Rename, no Delete, no Edit, because none of those can be
	/// honoured — so the only thing its menu offers is CUSTOMISE: a copy of
	/// it the user owns and may edit. Set on the presets drawer alone; a tile
	/// with no material guid (a graph TEMPLATE, which is instantiated by
	/// double-click) gets no menu at all.
	bool presetContextMenuAllowed = false;
	/// Injected by the module window: is a project scene open? (Phase 4:
	/// was UiManager::isSceneOpen). Null-safe: no probe = treated as closed.
	std::function<bool()> sceneOpenProbe;

	QSize itemSize;
	int numberOfItemPerRow;
	void addToListWidget(QListWidgetItem *item);
	static void updateThumbnailImage(QByteArray arr, QListWidgetItem *item);
	static void highlightNodeForInterval(int seconds, QListWidgetItem* item);
	static void stopHighlightedNode();
	static QVariantAnimation* anim;

private slots:
    void customContextMenu(QPoint pos);

protected:
    QMimeData * mimeData(const QList<QListWidgetItem *> &items) const override;
    void resizeEvent(QResizeEvent * event) override;

signals:
    void renameShader(QString guid);
    void exportShader(QString guid);
    /// ONE ROW, COPIED (MATERIAL_BUNDLE_SPEC 6: "New / Duplicate / Rename /
    /// Delete act on ONE row"). Duplicate was the one of the four with no
    /// gesture at all — a user who wanted a variant of a material had to
    /// create an empty one and rebuild the graph by hand.
    void duplicateShader(QString guid);
    void editShader(QString guid);
    void deleteShader(QString guid);
    void createShader(QString guid);
    void importShader(QString guid);
	void addToProject(QListWidgetItem *item);
	/// R18 — "give me my own copy of this shipped preset to edit".
	void customisePreset(QString presetGuid);

	void resizeItem(int size);
};

