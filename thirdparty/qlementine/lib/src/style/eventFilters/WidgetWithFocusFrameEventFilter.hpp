// SPDX-FileCopyrightText: Olivier Cléro <oclero@hotmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractScrollArea>
#include <QFocusFrame>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QEvent>

namespace oclero::qlementine {
class WidgetWithFocusFrameEventFilter : public QObject {
  Q_OBJECT
public:
  explicit WidgetWithFocusFrameEventFilter(QWidget* widget)
    : QObject(widget)
    , _widget(widget) {
    _focusFrame = new QFocusFrame(_widget);
  }

  // JAHSHAKA DIVERGENCE (2026-09-08) — see PROVENANCE.md.
  //
  // THE FRAME IS OURS TO DESTROY. It is born a child of the watched widget, but
  // attachFocusFrame() hands it to QFocusFrame::setWidget(), which REPARENTS it
  // to the enclosing scroll area's viewport (or the window). From that moment
  // the widget's destruction no longer takes the frame with it: Qt's
  // QFocusFrame only clears its pointer, and what is left behind is an
  // invisible widget that still filters events on every ancestor it was
  // watching. A host that rebuilds a panel's rows therefore accumulates one
  // dead frame — and one more filter on the shared ancestors — per row per
  // rebuild, for the lifetime of the process. (Measured in Jahshaka's
  // properties panel: +1188 live QFocusFrames over 180 selection changes, every
  // one of them still on the scroll viewport's event-filter list.)
  //
  // This filter is a child of the widget, so it dies exactly when the widget
  // does; the QPointer covers the other order (never attached, so the frame is
  // still a child and may be deleted first).
  ~WidgetWithFocusFrameEventFilter() override {
    delete _focusFrame.data();
  }

  bool eventFilter(QObject* watchedObject, QEvent* evt) override {
    const auto type = evt->type();

    if (!_added && watchedObject == _widget) {
      // Create the focus frame as late as possible to give
      // more chances to any parent (e.g. scrollarea) to already exist.
      // QEvent::Show isn't sufficient. We need to delay even more, so
      // waiting for the first QEvent::Paint is our only solution.
      if (type == QEvent::Paint) {
        QTimer::singleShot(0, this, [this]() {
          if (!_added) {
            _added = true;
            attachFocusFrame();
          }
        });
      }
    } else if (_added && type == QEvent::ParentChange) {
      // JAHSHAKA DIVERGENCE (2026-09-08) — see PROVENANCE.md.
      //
      // QFocusFrame::setWidget() picks the frame's parent by walking up from
      // the widget (in SH_FocusFrame_AboveWidget mode, which QlementineStyle
      // enables, that is the enclosing scroll area's viewport, a QToolBar, or
      // the window) and then caches it. Qt refreshes that choice when the
      // WIDGET's own parent changes, but not when an INTERMEDIATE ANCESTOR is
      // reparented: qfocusframe.cpp's `else if (d->showFrameAboveWidget)`
      // branch handles only Move/Resize/ZOrderChange for the ancestors it
      // watches.
      //
      // An application that detaches a whole subtree while keeping the widgets
      // alive therefore strands the frame in a hierarchy the widget has left,
      // and QFocusFramePrivate::updateSize() maps coordinates between two
      // unrelated widget trees on every geometry change — Qt answers with
      // "QWidget::mapTo(): parent must be in parent hierarchy" each time
      // (measured: 164,651 warnings in one 13-minute session of a host app
      // whose property panel reuses its accordion blades and orphans them with
      // setParent(nullptr) instead of deleting them).
      //
      // So watch the ancestors ourselves and re-run the derivation whenever
      // the span between the widget and the frame's parent changes shape.
      //
      // COALESCED (2026-09-08, second pass — the 1 fps regression): the
      // re-derivation is deferred to the end of the event-loop turn instead of
      // running inside the ParentChange dispatch. A host that detaches a
      // subtree and re-attaches it in the SAME turn (a properties panel
      // rebuilding its layout) sends two ParentChange events to an ancestor
      // that carries one of these filters per focusable descendant — dozens —
      // and each one used to answer with a full setWidget(nullptr)/setWidget()
      // cycle, i.e. a QWidget::setParent of the frame plus install/remove of
      // the frame's own event filters along the whole ancestor chain, twice.
      // Deferred and coalesced, the pair of ParentChanges collapses into ONE
      // check per filter, and that check finds the ancestry back where it
      // started and does nothing at all.
      scheduleRefresh();
    }

    return QObject::eventFilter(watchedObject, evt);
  }

private:
  /// Mirrors QFocusFrame::setWidget()'s parent derivation (SH_FocusFrame_AboveWidget
  /// mode) in ONE walk that also collects the ancestors to watch, so a refresh
  /// costs a single pass up the hierarchy.
  ///
  /// `chain` receives the widgets between the watched widget and the frame's
  /// parent INCLUSIVE — Qt watches the same span for Move/Resize; we need the
  /// frame's parent as well, because when the widget is orphaned that parent IS
  /// the detached subtree's root and its re-attachment is what has to bring the
  /// frame home.
  static QWidget* deriveFrameParent(QWidget* widget, QList<QWidget*>* chain = nullptr) {
    QWidget* prev = nullptr;
    for (auto* p = widget->parentWidget(); p; p = p->parentWidget()) {
      // The short-circuit matters and is copied deliberately: a TOP-LEVEL
      // scroll area stops the walk as a window, with isScrollArea still false,
      // so the frame lands on the scroll area itself and not on its viewport.
      bool isScrollArea = false;
      if (p->isWindow() || p->inherits("QToolBar") || (isScrollArea = p->inherits("QAbstractScrollArea"))) {
        // The previous one in the hierarchy is the scroll area's viewport.
        auto* const frameParent = (prev && isScrollArea) ? prev : p;
        // `prev` is already in the chain; only a frame parent that IS this
        // widget still has to be appended.
        if (chain && frameParent == p) {
          chain->append(p);
        }
        return frameParent;
      }
      if (chain) {
        chain->append(p);
      }
      prev = p;
    }
    return nullptr;
  }

  void attachFocusFrame() {
    if (!_focusFrame) {
      return;
    }
    _focusFrame->setWidget(_widget);
    QList<QWidget*> chain;
    deriveFrameParent(_widget, &chain);
    watchAncestors(chain);
  }

  /// One refresh per event-loop turn, at most. Same shape as the combo-box
  /// filter's deferral: a flag plus a zero timer bound to this filter, which is
  /// a child of the watched widget and dies with it, so a pending refresh can
  /// never outlive its subject.
  void scheduleRefresh() {
    if (_refreshPending) {
      return;
    }
    _refreshPending = true;
    QTimer::singleShot(0, this, [this]() {
      _refreshPending = false;
      refreshFocusFrame();
    });
  }

  void refreshFocusFrame() {
    if (_refreshing || !_focusFrame) {
      return;
    }
    _refreshing = true;

    // ONE walk answers both questions: is the frame's parent still the one the
    // derivation picks, and which ancestors have to be watched. When nothing
    // moved (the common case by far — an ancestor that was detached and
    // re-attached in the same turn, or a sibling subtree's ParentChange) both
    // answers are "unchanged" and the refresh touches nothing: no
    // QWidget::setParent, no event-filter churn.
    QList<QWidget*> chain;
    auto* const expectedParent = deriveFrameParent(_widget, &chain);
    if (_focusFrame->widget() != _widget || _focusFrame->parentWidget() != expectedParent) {
      // setWidget() early-returns when the widget is unchanged, so drop it
      // first to force the derivation to run again. Passing a widget that has
      // no parent at all simply hides the frame, which is what we want.
      _focusFrame->setWidget(nullptr);
      _focusFrame->setWidget(_widget);
    }
    watchAncestors(chain);

    _refreshing = false;
  }

  /// Installs this filter on `chain` and removes it from what left it. Bounded
  /// by the depth of the hierarchy: the list is rebuilt from the same walk that
  /// derived the frame parent, never appended to.
  void watchAncestors(const QList<QWidget*>& chain) {
    if (sameAsWatched(chain)) {
      return;
    }

    // Touch only what actually changed. This can run from inside an ancestor's
    // own event dispatch, and QObject::installEventFilter() compacts and
    // prepends to the very list QCoreApplicationPrivate::sendThroughObjectEventFilters()
    // is walking — reinstalling wholesale would shift that walk under itself and
    // could skip a sibling filter (a panel holds one of these per focusable
    // control, so there are dozens on the same ancestors).
    for (const auto& watched : _watchedAncestors) {
      if (watched && !chain.contains(watched.data())) {
        watched->removeEventFilter(this);
      }
    }
    for (auto* p : chain) {
      if (!_watchedAncestors.contains(p)) {
        p->installEventFilter(this);
      }
    }

    _watchedAncestors.clear();
    _watchedAncestors.reserve(chain.size());
    for (auto* p : chain) {
      _watchedAncestors.append(p);
    }
  }

  bool sameAsWatched(const QList<QWidget*>& chain) const {
    if (_watchedAncestors.size() != chain.size()) {
      return false;
    }
    for (int i = 0; i < chain.size(); ++i) {
      if (_watchedAncestors.at(i).data() != chain.at(i)) {
        return false;
      }
    }
    return true;
  }

private:
  QWidget* _widget{ nullptr };
  QPointer<QFocusFrame> _focusFrame;
  QList<QPointer<QWidget>> _watchedAncestors;
  bool _added{ false };
  bool _refreshing{ false };
  bool _refreshPending{ false };
};
} // namespace oclero::qlementine
