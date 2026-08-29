#pragma once
#include <QListWidget>
#include <QSize>
#include <QVariantAnimation>

struct shaderInfo {
	QString GUID;
	QString name;
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
    void editShader(QString guid);
    void deleteShader(QString guid);
    void createShader(QString guid);
    void importShader(QString guid);
	void addToProject(QListWidgetItem *item);

	void resizeItem(int size);
};

