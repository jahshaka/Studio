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
      refreshFocusFrame();
    }

    return QObject::eventFilter(watchedObject, evt);
  }

private:
  /// Mirrors QFocusFrame::setWidget()'s parent derivation (SH_FocusFrame_AboveWidget
  /// mode) so we can tell a stale frame parent from an up-to-date one.
  static QWidget* deriveFrameParent(QWidget* widget) {
    QWidget* prev = nullptr;
    for (auto* p = widget->parentWidget(); p; p = p->parentWidget()) {
      // The short-circuit matters and is copied deliberately: a TOP-LEVEL
      // scroll area stops the walk as a window, with isScrollArea still false,
      // so the frame lands on the scroll area itself and not on its viewport.
      bool isScrollArea = false;
      if (p->isWindow() || p->inherits("QToolBar") || (isScrollArea = p->inherits("QAbstractScrollArea"))) {
        // The previous one in the hierarchy is the scroll area's viewport.
        return (prev && isScrollArea) ? prev : p;
      }
      prev = p;
    }
    return nullptr;
  }

  void attachFocusFrame() {
    _focusFrame->setWidget(_widget);
    watchAncestors();
  }

  void refreshFocusFrame() {
    if (_refreshing) {
      return;
    }
    _refreshing = true;

    auto* const expectedParent = deriveFrameParent(_widget);
    if (_focusFrame->widget() != _widget || _focusFrame->parentWidget() != expectedParent) {
      // setWidget() early-returns when the widget is unchanged, so drop it
      // first to force the derivation to run again. Passing a widget that has
      // no parent at all simply hides the frame, which is what we want.
      _focusFrame->setWidget(nullptr);
      _focusFrame->setWidget(_widget);
    }
    watchAncestors();

    _refreshing = false;
  }

  /// Filters the widgets between the watched widget and the focus frame's parent
  /// INCLUSIVE — Qt watches the same span for Move/Resize; we need the frame's
  /// parent as well, because when the widget is orphaned that parent IS the
  /// detached subtree's root and its re-attachment is what has to bring the
  /// frame home.
  void watchAncestors() {
    QList<QWidget*> chain;
    auto* const frameParent = deriveFrameParent(_widget);
    for (auto* p = _widget->parentWidget(); p; p = p->parentWidget()) {
      chain.append(p);
      if (p == frameParent || p->isWindow()) {
        break;
      }
    }

    // Touch only what actually changed. This runs from inside an ancestor's own
    // ParentChange dispatch, and QObject::installEventFilter() compacts and
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

private:
  QWidget* _widget{ nullptr };
  QFocusFrame* _focusFrame{ nullptr };
  QList<QPointer<QWidget>> _watchedAncestors;
  bool _added{ false };
  bool _refreshing{ false };
};
} // namespace oclero::qlementine
