/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertyrows.h"

#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "ui/controls/accordionbladewidget.h"
#include "ui/controls/rowfit.h"
#include "ui/panels/propertywidget.h"

namespace PropertyRows {

namespace {

/// The pin mark a world-mode row appends to its own label (" *") is state, not
/// name — it must not change what the row is called or what it matches.
QString stripMarks(QString text)
{
    text = text.trimmed();
    while (text.endsWith(QLatin1Char('*')) || text.endsWith(QLatin1Char(' ')))
        text.chop(1);
    return text.trimmed();
}

/// Where a row keeps its NAME. Every generic control (.ui) calls it `label`;
/// a nested section's name is its blade title; the scrubbable rows
/// (DragFloatWidget / DragVector3Widget) build a bare QLabel as their first
/// child. A row with none of those has no name and is section-bound (§3.4.6).
QLabel *rowLabelWidget(QWidget *row)
{
    if (!row) return nullptr;
    if (auto *blade = qobject_cast<AccordianBladeWidget *>(row)) return blade->titleLabel();
    if (qobject_cast<PropertyWidget *>(row)) return nullptr;   // a container, not a row
    if (QLabel *named = row->findChild<QLabel *>(QStringLiteral("label"))) return named;
    for (QLabel *l : row->findChildren<QLabel *>()) {
        if (!l->text().trimmed().isEmpty()) return l;
    }
    return nullptr;
}

/// Substring match, and whether it lands at a WORD START. "ssr" at the start of
/// a word is what somebody typing "ssr" means; the same three letters inside a
/// longer word are a coincidence — see Registry::apply's strongOnly.
bool matchTerm(const QString &hay, const QString &term, bool *atWordStart)
{
    int from = 0;
    bool found = false;
    bool strong = false;
    while (true) {
        const int at = hay.indexOf(term, from);
        if (at < 0) break;
        found = true;
        if (at == 0 || !hay.at(at - 1).isLetterOrNumber()) { strong = true; break; }
        from = at + 1;
    }
    if (atWordStart) *atWordStart = strong;
    return found;
}

bool matchesAll(const QStringList &terms, const QString &hay, bool *allAtWordStart = nullptr)
{
    bool strong = true;
    for (const QString &t : terms) {
        bool wordStart = false;
        if (!matchTerm(hay, t, &wordStart)) { if (allAtWordStart) *allAtWordStart = false; return false; }
        strong = strong && wordStart;
    }
    if (allAtWordStart) *allAtWordStart = strong;
    return true;
}

}   // namespace

// ---------------------------------------------------------------------------

QString Entry::label() const
{
    if (!labelWidget) return staticLabel;
    // THE NAME, NOT THE PICTURE: a fitted label's text() is elided to whatever
    // width the dock gave it ("Sun D…"). RowFit::fullText is the ONE reader of
    // that distinction — Entry::label, sectionTitle and the blade's panelTitle
    // all go through it, because a section title elides exactly like a row's.
    return stripMarks(RowFit::fullText(labelWidget));
}

QString Entry::haystack() const
{
    QString out = label();
    if (!key.isEmpty()) out += QLatin1Char(' ') + key;
    for (const QString &k : keywords) out += QLatin1Char(' ') + k;
    return out.toLower();
}

// ---------------------------------------------------------------------------

Registry::Registry(QObject *parent) : QObject(parent) {}

Registry &Registry::instance()
{
    // Function-local: the panels that fill it are built and destroyed with the
    // window, and this outlives them by construction (the three deliberate
    // statics of ENGINE-3 have the same rationale).
    static Registry *sRegistry = new Registry;
    return *sRegistry;
}

QStringList Registry::termsFor(const QString &text)
{
    QStringList out;
    for (const QString &t : text.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts))
        out.append(t);
    return out;
}

void Registry::add(QWidget *container, QWidget *row)
{
    if (!container || !row) return;
    if (entries.contains(row)) retire(row);   // a row re-entering (never today)
    Entry e;
    e.widget = row;
    e.container = container;
    e.labelWidget = rowLabelWidget(row);
    // A CONTAINER'S OWN LIFETIME. A top-level blade is a key here and is
    // nobody's row, so nothing else would ever drop its row list — and these
    // are raw pointers used as keys, which Qt hands out again.
    const bool newContainer = !containers.contains(container);
    containers[container].rows.append(QPointer<QWidget>(row));
    if (newContainer)
        connect(container, &QObject::destroyed, this, [this, container]() { retire(container); });
    entries.insert(row, e);
    // A ROW THAT DIES WITHOUT BEING RETIRED (a blade destroyed with the window,
    // a control freed by its own parent) must leave the registry anyway: these
    // are RAW POINTERS used as keys, and Qt reuses addresses constantly — a
    // stale entry would be inherited by whatever widget lands on that address
    // next. The lambda only ever uses the pointer as a key.
    connect(row, &QObject::destroyed, this, [this, row]() { retire(row); });

    // BORN UNDER THE LIVE FILTER (§6.1). The Photon and sky panels rebuild
    // every row they own inside an edit; without this the new rows would appear
    // for one event-loop turn before the coalesced re-apply took them away.
    if (!liveTerms.isEmpty()) {
        const QString hay = entries[row].haystack();
        if (!hay.isEmpty() && !matchesAll(liveTerms, hay)) {
            entries[row].filteredOut = true;
            row->setVisible(false);
        }
    }
    scheduleChanged();
}

void Registry::retire(QWidget *row)
{
    if (!row) return;

    // THE CONTAINER HALF GOES FIRST, and unconditionally. A retired container
    // takes its rows with it (a nested section, the material PropertyWidget) —
    // and a TOP-LEVEL BLADE is a container that is nobody's ROW, so it has no
    // `entries` record at all. Testing `entries` first therefore returned
    // before dropping the blade's whole row list, leaving it keyed by a pointer
    // Qt is free to hand to the next widget it allocates.
    bool changed = false;
    auto own = containers.find(row);
    if (own != containers.end()) {
        const QVector<QPointer<QWidget>> rows = own->rows;
        containers.erase(own);
        changed = true;
        for (const QPointer<QWidget> &w : rows) if (w) retire(w.data());
    }

    auto it = entries.find(row);
    if (it != entries.end()) {
        if (QWidget *c = it->container) {
            auto node = containers.find(c);
            if (node != containers.end())
                node->rows.removeIf([row](const QPointer<QWidget> &p) { return p.data() == row; });
        }
        entries.erase(it);
        changed = true;
    }

    if (changed) scheduleChanged();
}

void Registry::identify(QWidget *row, const QString &key, const QStringList &keywords)
{
    auto it = entries.find(row);
    if (it == entries.end()) return;
    it->key = key;
    for (const QString &k : keywords)
        if (!it->keywords.contains(k)) it->keywords.append(k);
}

void Registry::nameRow(QWidget *row, const QString &label, const QStringList &keywords)
{
    auto it = entries.find(row);
    if (it == entries.end()) return;
    it->staticLabel = label;
    it->labelWidget = nullptr;
    for (const QString &k : keywords)
        if (!it->keywords.contains(k)) it->keywords.append(k);
}

void Registry::describe(QWidget *row, const QStringList &keywords)
{
    auto it = entries.find(row);
    if (it == entries.end()) return;
    for (const QString &k : keywords)
        if (!it->keywords.contains(k)) it->keywords.append(k);
}

bool Registry::isRegistered(const QWidget *row) const
{
    return entries.contains(const_cast<QWidget *>(row));
}

QWidget *Registry::rowFor(QWidget *w) const
{
    for (QWidget *p = w; p; p = p->parentWidget())
        if (entries.contains(p)) return p;
    return nullptr;
}

void Registry::setPanelVisible(QWidget *row, bool visible)
{
    if (!row) return;
    auto it = entries.find(row);
    if (it == entries.end()) { row->setVisible(visible); return; }
    it->panelVisible = visible;
    row->setVisible(visible && !it->filteredOut);
}

QString Registry::sectionTitle(const QWidget *section)
{
    auto *blade = qobject_cast<AccordianBladeWidget *>(const_cast<QWidget *>(section));
    if (!blade) return QString();
    QLabel *title = blade->titleLabel();
    return title ? stripMarks(RowFit::fullText(title)) : QString();
}

void Registry::purge(QWidget *container)
{
    auto node = containers.find(container);
    if (node == containers.end()) return;
    node->rows.removeIf([](const QPointer<QWidget> &p) { return p.isNull(); });
}

Result Registry::apply(QWidget *root, const QStringList &terms, bool strongOnly)
{
    Result result;
    if (!root) return result;
    liveTerms = terms;
    const QString chain = sectionTitle(root).toLower();
    // The blade's OWN title, judged by the same rule its rows are (strongOnly
    // included): a section kept on screen by a mid-word coincidence in its
    // title, while the column is showing word-start matches only, would be an
    // empty header.
    bool titleStrong = false;
    result.titleMatch = !terms.isEmpty() && matchesAll(terms, chain, &titleStrong)
                        && (!strongOnly || titleStrong);
    result.anyVisible = applyTo(root, terms, chain, strongOnly, result);
    if (terms.isEmpty()) result.anyVisible = true;
    return result;
}

/// THE WORD-START PASS (the column runs it before it applies anything).
///
/// "ssr" means the row CALLED ssr, not every row with those three letters
/// somewhere inside a word. So: if anything in the column matches every term at
/// a word start, only word-start matches are kept; if nothing does, plain
/// substring matching stands and a half-remembered fragment still finds its row.
bool Registry::hasStrongMatch(QWidget *root, const QStringList &terms) const
{
    if (!root || terms.isEmpty()) return false;
    return strongIn(root, terms, sectionTitle(root).toLower());
}

bool Registry::strongIn(QWidget *container, const QStringList &terms,
                        const QString &chain) const
{
    auto node = containers.constFind(container);
    if (node == containers.constEnd()) return false;
    for (const QPointer<QWidget> &ptr : node->rows) {
        QWidget *row = ptr.data();
        if (!row) continue;
        auto it = entries.constFind(row);
        if (it == entries.constEnd()) continue;
        const QString hay = it->haystack();
        if (containers.contains(row)) {
            const QString subChain = hay.isEmpty() ? chain : chain + QLatin1Char(' ') + hay;
            bool strong = false;
            if (matchesAll(terms, subChain, &strong) && strong) return true;
            if (strongIn(row, terms, subChain)) return true;
            continue;
        }
        if (hay.isEmpty() || !it->panelVisible) continue;
        bool strong = false;
        if (matchesAll(terms, hay + QLatin1Char(' ') + chain, &strong) && strong) return true;
    }
    return false;
}

bool Registry::applyTo(QWidget *container, const QStringList &terms,
                       const QString &chain, bool strongOnly, Result &result)
{
    purge(container);
    auto node = containers.find(container);
    if (node == containers.end()) return false;

    const bool filtering = !terms.isEmpty();
    bool anyVisible = false;
    QVector<QWidget *> sectionBound;

    // The rows are walked on a COPY: a nested section's apply can retire rows
    // (it cannot today, but a registry that recursed over a live vector would
    // be one rebuild away from a dangling iterator).
    const QVector<QPointer<QWidget>> rows = node->rows;
    for (const QPointer<QWidget> &ptr : rows) {
        QWidget *row = ptr.data();
        if (!row) continue;
        auto it = entries.find(row);
        if (it == entries.end()) continue;

        const QString hay = it->haystack();
        const bool isContainer = containers.contains(row);

        if (isContainer) {
            const QString subChain = hay.isEmpty() ? chain : chain + QLatin1Char(' ') + hay;
            const bool subAny = applyTo(row, terms, subChain, strongOnly, result);
            bool selfStrong = false;
            const bool selfMatch = filtering && matchesAll(terms, subChain, &selfStrong)
                                   && (!strongOnly || selfStrong);
            const bool visible = !filtering || subAny || selfMatch;
            it->filteredOut = filtering && !visible;
            // A SECTION WITH A MATCH OPENS (§3.4.5); one without closes with its
            // rows. The panel holds the snapshot that puts them back.
            if (auto *blade = qobject_cast<AccordianBladeWidget *>(row)) {
                if (filtering) { if (visible) blade->expand(); else blade->collapse(); }
            }
            row->setVisible(it->panelVisible && !it->filteredOut);
            anyVisible = anyVisible || (visible && it->panelVisible);
            continue;
        }

        if (hay.isEmpty()) { sectionBound.append(row); continue; }

        bool strong = false;
        const bool visible = !filtering
                             || (matchesAll(terms, hay + QLatin1Char(' ') + chain, &strong)
                                 && (!strongOnly || strong));
        it->filteredOut = filtering && !visible;
        row->setVisible(it->panelVisible && !it->filteredOut);
        if (it->panelVisible) { visible ? ++result.visible : ++result.hidden; }
        anyVisible = anyVisible || (visible && it->panelVisible);
    }

    // SECTION-BOUND ROWS (§3.4.6) — the reset buttons, the ramps, the asset
    // rows: nothing to match, so they follow the section they are in.
    bool chainStrong = false;
    const bool sectionVisible = !filtering || anyVisible
                                || (matchesAll(terms, chain, &chainStrong)
                                    && (!strongOnly || chainStrong));
    for (QWidget *row : sectionBound) {
        auto it = entries.find(row);
        if (it == entries.end()) continue;
        it->filteredOut = filtering && !sectionVisible;
        row->setVisible(it->panelVisible && !it->filteredOut);
    }
    return anyVisible;
}

QVector<Registry::Listing> Registry::list(QWidget *root) const
{
    QVector<Listing> out;
    if (!root) return out;
    collect(root, QStringList{ sectionTitle(root) }, out);
    return out;
}

void Registry::collect(QWidget *container, const QStringList &sections,
                       QVector<Listing> &out) const
{
    auto node = containers.constFind(container);
    if (node == containers.constEnd()) return;
    for (const QPointer<QWidget> &ptr : node->rows) {
        QWidget *row = ptr.data();
        if (!row) continue;
        auto it = entries.constFind(row);
        if (it == entries.constEnd()) continue;
        if (containers.contains(row)) {
            QStringList deeper = sections;
            const QString title = it->label();
            if (!title.isEmpty()) deeper.append(title);
            collect(row, deeper, out);
            continue;
        }
        Listing l;
        l.sections = sections;
        l.label = it->label();
        l.key = it->key;
        l.keywords = it->keywords;
        l.panelVisible = it->panelVisible;
        l.filteredOut = it->filteredOut;
        l.visible = it->panelVisible && !it->filteredOut;
        out.append(l);
    }
}

void Registry::scheduleChanged()
{
    if (changePending) return;
    changePending = true;
    QTimer::singleShot(0, this, [this]() {
        changePending = false;
        emit rowsChanged();
    });
}

}   // namespace PropertyRows
