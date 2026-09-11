/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/toast.h"

#include <QApplication>
#include <QEvent>
#include <QScreen>
#include <QTimer>
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"

namespace {
/// How long a toast stays up when the caller does not say.
constexpr int kDefaultHoldMs = 1650;
}

Toast::Toast(QWidget *parent) : QFrame(parent)
{
	setObjectName("Toast");
	toastLayout = new QVBoxLayout;
	setLayout(toastLayout);
	caption = new QLabel;
	caption->setObjectName("Caption");
	info = new QLabel;
	info->setObjectName("Info");
	toastLayout->addWidget(caption);
	toastLayout->addSpacing(6);
	toastLayout->addWidget(info);

	setWindowFlags(
		Qt::Window						| // Add if popup doesn't show up
		Qt::FramelessWindowHint			| // No window border
		Qt::WindowDoesNotAcceptFocus	| // No focus
		Qt::WindowStaysOnTopHint		  // Always on top
	);

	// Classic's toast sheet; under Qlementine the style's own rounded panel
	// on the theme's surface, with a larger caption.
	setStyleSheet(StyleSheet::ToastPanel());
	ThemeRoles::setSurface(this, ThemeRoles::Surface::Panel);
	ThemeRoles::setFrame(this, QFrame::StyledPanel);
	ThemeRoles::setTextSize(caption, 16);

	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setLineWidth(1);

	mHold.setSingleShot(true);
	connect(&mHold, &QTimer::timeout, this, &Toast::hide);

	// A toast follows its window: moved, resized or maximised, it stays on its
	// anchor instead of hanging where the window used to be.
	if (QWidget *owner = ownerWindow()) owner->installEventFilter(this);
}

void Toast::setAnchor(Anchor anchor, QWidget *widget)
{
	mAnchor = anchor;
	mAnchorWidget = widget;
	if (isVisible()) reposition();
}

void Toast::showToast(const QString &title, const QString &text, int holdMs)
{
	caption->setText(title);
	info->setText(text);
	adjustSize();
	reposition();
	mHold.start(holdMs > 0 ? holdMs : kDefaultHoldMs);
	show();
}

QWidget *Toast::ownerWindow() const
{
	if (QWidget *p = parentWidget()) return p->window();
	return QApplication::activeWindow();
}

void Toast::showEvent(QShowEvent *event)
{
	QFrame::showEvent(event);
	// The size is only final once the style has laid the labels out; place it
	// again here so the FIRST appearance is centred too (the very complaint:
	// the toast that shows once, off to one side, and is gone before anything
	// would have corrected it).
	reposition();
}

bool Toast::eventFilter(QObject *watched, QEvent *event)
{
	if (isVisible() && watched == ownerWindow()
	    && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
		reposition();
	return QFrame::eventFilter(watched, event);
}

QRect Toast::geometryInWindow() const
{
	QWidget *owner = ownerWindow();
	if (!owner) return geometry();
	return QRect(owner->mapFromGlobal(geometry().topLeft()), size());
}

// THE PLACEMENT. A Toast is a frameless TOP-LEVEL, so move() takes SCREEN
// coordinates — every anchor is therefore computed through mapToGlobal on the
// widget it is anchored to, never by adding a window origin to a local point
// (audit F-D4: the two shell call sites did exactly that, which is right only
// while the window sits at the screen's origin — i.e. on the test rig).
void Toast::reposition()
{
	QWidget *owner = ownerWindow();
	const QSize mine = sizeHint().expandedTo(size());
	QWidget *anchorWidget = (mAnchor == Anchor::WidgetTop && mAnchorWidget) ? mAnchorWidget.data() : owner;
	if (!anchorWidget) return;

	const QRect area(anchorWidget->mapToGlobal(QPoint(0, 0)), anchorWidget->size());
	QPoint topLeft;
	switch (mAnchor) {
	case Anchor::WidgetTop:
		topLeft = QPoint(area.center().x() - mine.width() / 2, area.top() + mMargin);
		break;
	case Anchor::WindowCentre:
		topLeft = QPoint(area.center().x() - mine.width() / 2, area.center().y() - mine.height() / 2);
		break;
	case Anchor::WindowBottom:
	default:
		// BOTTOM-CENTRE OF THE APP WINDOW — the owner's rule for every message
		// that is not pinned to something else.
		topLeft = QPoint(area.center().x() - mine.width() / 2,
		                 area.bottom() - mine.height() - mMargin);
		break;
	}
	// Never off the owner's area: a long message on a small window would push
	// the toast past the edge, where nobody reads it.
	topLeft.setX(qBound(area.left(), topLeft.x(), qMax(area.left(), area.right() - mine.width())));
	topLeft.setY(qBound(area.top(), topLeft.y(), qMax(area.top(), area.bottom() - mine.height())));
	move(topLeft);
	resize(mine);
}
