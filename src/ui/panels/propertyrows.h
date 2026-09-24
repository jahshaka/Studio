/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROPERTYROWS_H
#define PROPERTYROWS_H

// THE PROPERTY ROW REGISTRY (PROPERTY_FILTER_SPEC §3, lane PROPERTY-FILTER-1).
//
// A row in the right column used to have no identity at all: it was a widget
// pointer held as a panel member, with its name painted into a child QLabel and
// its visibility owned by whichever panel built it. Nothing could ask "what
// rows are on this column" and nothing could hide one without fighting the
// panel that owns it.
//
// This is the whole model, and it is filled from the TWO CHOKE POINTS every row
// already passes through — AccordianBladeWidget::addRow and
// PropertyWidget::addRow — so the 175 row-creation sites need no edits and a
// panel that rebuilds its rows re-registers by construction.
//
// Two things follow from it:
//
//   * TEXT REACHES EVERY ROW. An entry carries the row's live label (read from
//     the control's own QLabel, so a label that mutates — the world-mode pin
//     mark " *" — still matches), an optional stable KEY ("world.override:ssr",
//     "postFx.exposure", "roughness") and optional KEYWORDS curated beside the
//     row. "ssr" finds "Screen-Space Reflections" through the key; "reflections"
//     finds it through the section title chain.
//
//   * VISIBILITY HAS TWO INPUTS AND ONE OWNER. `panelVisible` is the panel's
//     intent (a spot row on a point light, the AA driver readout); `filteredOut`
//     is the filter's verdict; the effective visibility is the AND of them,
//     applied here. A panel refresh under a live filter can no longer un-hide a
//     filtered-out row, and clearing the filter cannot show a row the panel
//     means to hide — which is why the 74 setVisible/hide/show sites in the
//     property panels call setPanelVisible() instead.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class QLabel;
class QWidget;

namespace PropertyRows {

/// One registered row. `widget` is null once the row has been destroyed
/// (retire() drops it earlier, at the moment the blade retires it — a row in
/// the blade's retired-row ring must not be counted or matched).
struct Entry {
    QPointer<QWidget> widget;
    QPointer<QWidget> container;    ///< the blade (or PropertyWidget) it entered
    QPointer<QLabel>  labelWidget;  ///< read LIVE: labels mutate (the pin mark)
    QString           staticLabel;  ///< fallback when the row has no QLabel
    QString           key;
    QStringList       keywords;
    bool              panelVisible = true;
    bool              filteredOut  = false;

    /// The row's displayed name, pin mark stripped.
    QString label() const;
    /// label + key + keywords, lower-cased — what a term is matched against
    /// (together with the section-title chain).
    QString haystack() const;
};

/// What one apply() over a top-level blade found.
struct Result {
    int  visible = 0;       ///< rows the filter kept (panel-hidden rows excluded)
    int  hidden  = 0;       ///< rows the filter removed
    bool anyVisible = false;
    bool titleMatch = false;  ///< the blade's own title satisfied every term
};

class Registry : public QObject
{
    Q_OBJECT
public:
    static Registry &instance();

    // --- the choke points ---------------------------------------------------
    /// A row entered `container` (a blade, or a PropertyWidget for the material
    /// rows). Called from the two addRow()s and nowhere else.
    void add(QWidget *container, QWidget *row);
    /// The row is leaving the panel (retired by clearPanel, or destroyed).
    void retire(QWidget *row);

    // --- identity, added where it is known ----------------------------------
    /// Stable key + optional synonyms. Called beside the row's creation site.
    void identify(QWidget *row, const QString &key,
                  const QStringList &keywords = QStringList());
    /// Synonyms alone (a row whose label is its only identity).
    void describe(QWidget *row, const QStringList &keywords);
    /// A NAME for a row that has no label of its own (a caller-built widget, a
    /// grid of fields like the transform editor). Without it the row is
    /// section-bound: findable only through its section.
    void nameRow(QWidget *row, const QString &label,
                 const QStringList &keywords = QStringList());

    // --- the two-input visibility law ---------------------------------------
    /// The PANEL's intent. Falls back to plain setVisible() for a widget that
    /// is not a registered row, so a call site never has to ask.
    void setPanelVisible(QWidget *row, bool visible);
    bool isRegistered(const QWidget *row) const;
    /// How many CONTAINERS (blades, nested sections, material lists) the
    /// registry is holding row lists for. A diagnostic, and the thing
    /// ui.properties_filter asserts does not grow: these are raw pointers used
    /// as keys, and Qt reuses addresses.
    int trackedContainers() const { return containers.size(); }
    /// The registered ROW `w` belongs to — `w` itself, or the nearest ancestor
    /// that is a row. (A binding is often made on a control INSIDE the row: the
    /// colour picker inside a ColorValueWidget.) Null if none.
    QWidget *rowFor(QWidget *w) const;

    // --- the filter ---------------------------------------------------------
    /// Applies `terms` (already lower-cased, whitespace-split) to the subtree
    /// under `root` — a top-level blade — and puts every row's effective
    /// visibility on screen. Empty terms = no filter: every row back to its
    /// panelVisible and every nested section back to `restore`d expand state.
    Result apply(QWidget *root, const QStringList &terms, bool strongOnly = false);
    /// Whether anything under `root` matches every term at a WORD START — the
    /// column asks every blade before it applies, so "ssr" can find the row
    /// CALLED ssr rather than every row with those letters inside a word.
    bool hasStrongMatch(QWidget *root, const QStringList &terms) const;

    /// The terms the column is filtering by right now. A row registered while
    /// a filter is live is judged at birth against these, so a panel that
    /// rebuilds its rows mid-filter never flashes them on screen.
    const QStringList &terms() const { return liveTerms; }

    /// Splits a user's text into match terms (trimmed, lower-cased, no empties).
    static QStringList termsFor(const QString &text);

    /// The rows under `root`, in layout order, for a verb or a test.
    struct Listing {
        QStringList sections;   ///< the title chain, outermost first
        QString label;
        QString key;
        QStringList keywords;
        bool panelVisible = true;
        bool visible = true;
        bool filteredOut = false;
        /// The row itself — what readRow/driveRow below act on.
        QPointer<QWidget> widget;
    };
    QVector<Listing> list(QWidget *root) const;

    /// The section title of a blade (its content_title), or an empty string.
    static QString sectionTitle(const QWidget *section);

signals:
    /// Rows were added or retired (coalesced to one emission per event-loop
    /// turn). The properties panel re-applies its filter on it — the Photon and
    /// sky panels rebuild every row they own on every edit.
    void rowsChanged();

private:
    explicit Registry(QObject *parent = nullptr);

    struct Node {
        QVector<QPointer<QWidget>> rows;   ///< in insertion (= layout) order
    };

    /// The recursion behind apply(): `chain` is the lower-cased text of every
    /// section title above (and including) `container`.
    bool applyTo(QWidget *container, const QStringList &terms,
                 const QString &chain, bool strongOnly, Result &result);
    bool strongIn(QWidget *container, const QStringList &terms,
                  const QString &chain) const;
    void collect(QWidget *container, const QStringList &sections,
                 QVector<Listing> &out) const;
    void scheduleChanged();
    void purge(QWidget *container);

    QHash<QWidget *, Entry> entries;
    QHash<QWidget *, Node>  containers;
    QStringList liveTerms;
    bool changePending = false;
};

inline Registry &registry() { return Registry::instance(); }

/// Free-function shorthands — the call sites read better for it.
inline void identify(QWidget *row, const QString &key,
                     const QStringList &keywords = QStringList())
{ Registry::instance().identify(row, key, keywords); }
inline void describe(QWidget *row, const QStringList &keywords)
{ Registry::instance().describe(row, keywords); }
inline void nameRow(QWidget *row, const QString &label,
                    const QStringList &keywords = QStringList())
{ Registry::instance().nameRow(row, label, keywords); }
/// Identify by a control that may be INSIDE the row (see Registry::rowFor).
inline void identifyControl(QWidget *control, const QString &key,
                            const QStringList &keywords = QStringList())
{ Registry::instance().identify(Registry::instance().rowFor(control), key, keywords); }
inline void setPanelVisible(QWidget *row, bool visible)
{ Registry::instance().setPanelVisible(row, visible); }

// --- THE ROW AS A USER MEETS IT (editor.propertyRow) ------------------------
// A row's control, found inside the row by what it IS — a check box, a combo,
// a number field, or only a label — so a script can read and drive a panel
// row by its stable key without knowing which widget class the panel used.

/// {control: "check"|"combo"|"number"|"label"|"other", value, enabled, items?}
/// `enabled` is the control's EFFECTIVE state (a greyed row is false).
QVariantMap readRow(QWidget *row);
/// Performs the gesture a user makes on the row's control: a check box is
/// CLICKED when it does not already hold `value`; a combo takes an index or an
/// item's text; a number field is FOCUSED, takes the value and a Return (the
/// typed session a person's click-type-Return makes — one undo step; a field
/// whose window cannot take focus is refused). A field rounds the value to its
/// own decimals before the document sees it. Refuses a greyed row, a label,
/// and a value the control cannot hold, with the reason in `error`.
bool driveRow(QWidget *row, const QVariant &value, QString *error);

}   // namespace PropertyRows

#endif // PROPERTYROWS_H
