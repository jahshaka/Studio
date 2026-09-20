/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/componentspropertywidget.h"

#include <functional>

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QSet>
#include <QTreeWidget>

#include "services/nodecomponents.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "ui/controls/nodeicons.h"
#include "viewport/ieditorviewport.h"

namespace {
/// The node guid a row carries.
constexpr int kGuidRole = Qt::UserRole;
/// How tall the list may grow before it scrolls: tall enough to read a normal
/// model at a glance, short enough that a two-hundred-part import does not push
/// every other section off the column.
constexpr int kMaxVisibleRows = 12;
}   // namespace

iris::SceneNodePtr ComponentsPropertyWidget::subjectFor(const iris::SceneNodePtr &node)
{
    iris::SceneNodePtr n = node;
    // `attached` is the document's word for "this is one of my parent's parts"
    // — set by the importer on every child of a model and by the outliner's own
    // group gesture. Climbing it lands on the row the outliner draws.
    while (n && n->isAttached()) {
        const iris::SceneNodePtr parent = n->getParent();
        if (!parent || parent->isRootNode()) break;
        n = parent;
    }
    return n;
}

bool ComponentsPropertyWidget::applies(const iris::SceneNodePtr &node)
{
    const iris::SceneNodePtr group = subjectFor(node);
    return group && group->childCount() > 0;
}

ComponentsPropertyWidget::ComponentsPropertyWidget()
{
    setPanelTitle("Components");

    tree = new QTreeWidget();
    tree->setColumnCount(3);
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setExpandsOnDoubleClick(false);   // a double-click FRAMES, it does not fold
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree->header()->resizeSection(1, 24);
    tree->header()->resizeSection(2, 24);
    tree->setToolTip(tr(
        "The parts of this object.\n\n"
        "An imported model is one object made of many pieces; this is the list of them. "
        "Click a part to select it, Shift or Ctrl-click to select several, double-click to "
        "frame it in the viewport.\n\n"
        "The eye and the padlock show whether a part is hidden and whether it is locked. "
        "They are shown here, not edited here - the Hierarchy panel's own eye and padlock "
        "are the controls, so one gesture is undone in one place."));

    // NAMED, so the Properties filter can find it by text — an unnamed row is
    // section-bound (PROPERTY_FILTER_SPEC §3.4.6).
    this->addWidgetToContent(tree, tr("Parts"),
                             { QStringLiteral("components"), QStringLiteral("parts"),
                               QStringLiteral("pieces"), QStringLiteral("children") });

    connect(tree, &QTreeWidget::itemSelectionChanged,
            this, &ComponentsPropertyWidget::listSelectionChanged);
    connect(tree, &QTreeWidget::itemDoubleClicked,
            this, &ComponentsPropertyWidget::rowActivated);

    expand();
}

void ComponentsPropertyWidget::setServices(StudioServices *s)
{
    if (services == s) return;
    if (services && services->selection) services->selection->disconnect(this);
    services = s;
    // BOTH WAYS (R14): a selection made in the hierarchy, the viewport or a
    // verb repaints this list's highlight. The SET signal, not the primary
    // one: a Ctrl-click that adds a second part never changes the primary.
    if (services && services->selection) {
        connect(services->selection, &SelectionService::selectionSetChanged,
                this, [this]() { applyEditorSelection(); });
    }
}

QString ComponentsPropertyWidget::listSignature() const
{
    // WHAT A REBUILD WOULD PRODUCE. Everything the rows draw is in here, so a
    // call that would draw the same rows can be answered with a repaint — and
    // anything that really changed (a part renamed, hidden, locked, added) is a
    // different string and does rebuild.
    QString sig;
    if (!subject) return sig;
    sig += subject->getGUID();
    for (const nodecomponents::Part &part : nodecomponents::partsOf(subject)) {
        sig += QLatin1Char('\n');
        sig += QString::number(part.depth);
        sig += QLatin1Char('|');
        sig += part.node->getGUID();
        sig += QLatin1Char('|');
        sig += part.node->getName();
        sig += QLatin1Char('|');
        sig += QString::number(int(part.node->getSceneNodeType()));
        sig += part.node->isVisible() ? QLatin1Char('v') : QLatin1Char('-');
        sig += part.node->isPickable() ? QLatin1Char('-') : QLatin1Char('l');
    }
    return sig;
}

void ComponentsPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    subject = subjectFor(sceneNode);

    // RE-LAY BY DIFFERENCE. Selecting a part of a model calls this with a
    // different node every time, and every one of those calls resolves to the
    // SAME group and the same rows — so the common case (the one a user
    // generates by clicking around inside one model) repaints a highlight and
    // builds nothing. This is the row-level form of the law SELECT-COST-1 gave
    // the column: a selection change moves nothing in or out that did not
    // change.
    const QString sig = listSignature();
    if (sig == builtSignature && tree->topLevelItemCount() > 0) {
        ++refreshes;
        applyEditorSelection();
        return;
    }
    builtSignature = sig;
    ++rebuilds;

    applying = true;
    tree->clear();
    partsByGuid.clear();
    if (subject) {
        // The flattened list carries a depth; the rows are nested back up from
        // it with a stack, so the indentation a person reads is the hierarchy's
        // own and not a second idea of it.
        QVector<QTreeWidgetItem *> byDepth;   // byDepth[d] = the last row at depth d+1
        for (const nodecomponents::Part &part : nodecomponents::partsOf(subject)) {
            auto *item = new QTreeWidgetItem();
            item->setText(0, part.node->getName());
            item->setIcon(0, nodeicons::forType(part.node->getSceneNodeType()));
            item->setData(0, kGuidRole, part.node->getGUID());
            item->setIcon(1, nodeicons::visibility(part.node->isVisible()));
            item->setIcon(2, nodeicons::lock(!part.node->isPickable()));
            item->setToolTip(0, part.node->getName());
            partsByGuid.insert(part.node->getGUID(), part.node);

            QTreeWidgetItem *parent = (part.depth >= 2 && byDepth.size() >= part.depth - 1)
                                          ? byDepth[part.depth - 2] : nullptr;
            if (parent) parent->addChild(item);
            else tree->addTopLevelItem(item);

            byDepth.resize(part.depth);
            byDepth[part.depth - 1] = item;
        }
        tree->expandAll();
    }
    applying = false;

    // A LIST THAT IS ALWAYS THE SAME HEIGHT wastes the column on a two-part
    // model and hides a twenty-part one. It grows with the rows and stops.
    //
    // MEASURED, NOT GUESSED. A constant row height is a row height under one
    // theme at one font: this list first shipped with 20 px against the 28 the
    // style actually draws, and its LAST ROW WAS CLIPPED OUT OF THE VIEWPORT —
    // present, unreachable, and invisible to anything but a click that missed.
    const int shown = qMin(kMaxVisibleRows, qMax(1, visibleRowCount()));
    const int rowHeight = tree->topLevelItemCount() > 0
                              ? qMax(1, tree->visualItemRect(tree->topLevelItem(0)).height())
                              : tree->fontMetrics().height() + 8;
    tree->setFixedHeight(shown * rowHeight + 2 * tree->frameWidth() + 2);

    applyEditorSelection();
}

void ComponentsPropertyWidget::applyEditorSelection()
{
    if (!tree) return;
    QSet<QString> selected;
    QString primaryGuid;
    if (services && services->selection) {
        for (const auto &node : services->selection->selectedSet())
            if (node) selected.insert(node->getGUID());
        if (const auto primary = services->selection->selected()) primaryGuid = primary->getGUID();
    }

    applying = true;
    // THE PRIMARY IS ALSO THE VIEW'S CURRENT ROW. Painting the highlight with
    // setSelected() alone leaves the view with no current index, and a view
    // with no current index has nothing for the NEXT Shift-click to extend
    // FROM — the range gesture silently degrades to a plain click. NoUpdate
    // because the selection itself is what the loop below writes.
    QTreeWidgetItem *primaryItem = nullptr;
    std::function<void(QTreeWidgetItem *)> paint = [&](QTreeWidgetItem *item) {
        const QString guid = item->data(0, kGuidRole).toString();
        const bool on = selected.contains(guid);
        item->setSelected(on);
        if (on && !primaryItem && !primaryGuid.isEmpty() && guid == primaryGuid) primaryItem = item;
        for (int i = 0; i < item->childCount(); ++i) paint(item->child(i));
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) paint(tree->topLevelItem(i));
    if (primaryItem) tree->setCurrentItem(primaryItem, 0, QItemSelectionModel::NoUpdate);
    applying = false;
}

void ComponentsPropertyWidget::listSelectionChanged()
{
    if (applying) return;   // we wrote it; it is not a gesture
    if (!services || !services->selection || !subject) return;

    // WHAT THE USER'S MODIFIERS MEANT is already in the list's own selection:
    // an ExtendedSelection view resolves plain click, Shift-range and
    // Ctrl-toggle for us, so the gesture reaches the editor as the SET it
    // produced rather than as three cases this widget would have to guess at.
    QList<iris::SceneNodePtr> nodes;
    const QList<QTreeWidgetItem *> picked = tree->selectedItems();
    for (QTreeWidgetItem *item : picked) {
        const iris::SceneNodePtr part = partsByGuid.value(item->data(0, kGuidRole).toString());
        if (part) nodes.append(part);
    }
    // Clicking into empty space below the rows deselects everything in the
    // list; that is not "deselect the object" — the group stays selected.
    if (nodes.isEmpty()) return;

    // THE LAST ROW THE USER TOUCHED IS THE PRIMARY (the node the rest of the
    // column, the gizmo and every single-target verb act on), exactly as a
    // click in the outliner behaves. `currentItem` is what the view just moved
    // to, so it leads the list.
    if (QTreeWidgetItem *current = tree->currentItem()) {
        const QString guid = current->data(0, kGuidRole).toString();
        for (int i = 0; i < nodes.size(); ++i)
            if (nodes[i]->getGUID() == guid) { nodes.move(i, 0); break; }
    }
    services->selection->select(nodes);
}

void ComponentsPropertyWidget::rowActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item || !sceneView || !subject) return;
    const iris::SceneNodePtr part = partsByGuid.value(item->data(0, kGuidRole).toString());
    if (part) sceneView->focusOnNode(part);
}

int ComponentsPropertyWidget::visibleRowCount() const
{
    int n = 0;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
        ++n;
        if (!item->isExpanded()) return;
        for (int i = 0; i < item->childCount(); ++i) walk(item->child(i));
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
    return n;
}

QStringList ComponentsPropertyWidget::rowLabels() const
{
    QStringList out;
    if (!tree) return out;
    std::function<void(QTreeWidgetItem *, int)> walk = [&](QTreeWidgetItem *item, int depth) {
        out.append(QStringLiteral("%1:%2").arg(depth).arg(item->text(0)));
        for (int i = 0; i < item->childCount(); ++i) walk(item->child(i), depth + 1);
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i), 1);
    return out;
}
