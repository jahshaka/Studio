#pragma once

// THE MEMBERS PANEL — what this material is MADE OF (MATERIAL_BUNDLE_SPEC §6,
// phase 2). One row per member: its name, the slot it fills or the graph node
// that holds it, its size, how many things use it, and whether it is a
// picture somebody picked or a map the bake produced.
//
// Two actions, and both are the verbs' own implementation
// (services/materialmembers.h — `materials.cleanUnused` and
// `materials.makeUnique` call exactly these functions):
//
//   CLEAN UNUSED  lists before it removes, always. The badge counts what is
//                 unused; the button opens the list and asks. Bytes are never
//                 removed here — that is `assets.gc`.
//   MAKE UNIQUE   gives THIS material its own copy of a shared texture. The
//                 button is enabled only when the selected member is shared
//                 (used by more than one), because that is the only case in
//                 which it means anything.
//
// The panel is a READER of the bundle: it never writes a definition itself.
// `refresh` re-reads, and the page calls it after every commit.

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class Database;
class Project;

namespace materials
{

class MembersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MembersPanel(QWidget *parent = nullptr);

    void setDatabase(Database *db) { mDb = db; }
    void setProject(Project *project) { mProject = project; }
    /// Which bundle is open. Empty = nothing to show.
    void setMaterial(const QString &guid);
    QString material() const { return mMaterial; }

    void refresh();

signals:
    /// A member changed identity (Make unique) or went (Clean unused): the
    /// page re-reads the definition and re-dresses the scene.
    void membersChanged(const QString &materialGuid);

private:
    void cleanUnused();
    void makeUnique();
    void selectionChanged();

    Database *mDb = nullptr;
    Project  *mProject = nullptr;
    QString   mMaterial;

    QLabel       *mSummary = nullptr;
    QTreeWidget  *mList = nullptr;
    QPushButton  *mCleanButton = nullptr;
    QPushButton  *mUniqueButton = nullptr;
};

}   // namespace materials
