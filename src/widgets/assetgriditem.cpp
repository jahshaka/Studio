#include "assetgriditem.h"

// local
AssetGridItem::AssetGridItem(QJsonObject details, QImage image, QWidget *parent) : QWidget(parent) {
	this->metadata = details;
	url = details["icon_url"].toString();
	selected = false;
	auto layout = new QGridLayout;
	layout->setMargin(0);
	layout->setSpacing(0);
	pixmap = QPixmap::fromImage(image);
	gridImageLabel = new QLabel;
	gridImageLabel->setPixmap(pixmap.scaledToHeight(164, Qt::SmoothTransformation));
	gridImageLabel->setAlignment(Qt::AlignCenter);

	layout->addWidget(gridImageLabel, 0, 0);
	textLabel = new QLabel(details["name"].toString());

	textLabel->setWordWrap(true);
	textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	layout->addWidget(textLabel, 1, 0);

	setMinimumWidth(180);
	setMaximumWidth(180);
	setMinimumHeight(196);
	setMaximumHeight(196);

	textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
		"border-left: 3px solid rgba(0, 0, 0, 3%); border-bottom: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
	gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 3%); border-top: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");

	setStyleSheet("background: #272727");
	setLayout(layout);
	setCursor(Qt::PointingHandCursor);

	connect(this, SIGNAL(hovered()), SLOT(dimHighlight()));
	connect(this, SIGNAL(left()), SLOT(noHighlight()));
}

void AssetGridItem::setTile(QPixmap pix) {
	pixmap = pix;
	gridImageLabel->setPixmap(pixmap.scaledToHeight(164, Qt::SmoothTransformation));
	gridImageLabel->setAlignment(Qt::AlignCenter);
}

void AssetGridItem::enterEvent(QEvent *event) {
	QWidget::enterEvent(event);
	emit hovered();
}

void AssetGridItem::leaveEvent(QEvent *event) {
	QWidget::leaveEvent(event);
	emit left();
}

void AssetGridItem::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
		emit singleClicked(this);
	}
}

void AssetGridItem::dimHighlight() {
    if (!selected) {
        textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
            "border-left: 3px solid rgba(0, 0, 0, 10%); border-bottom: 3px solid rgba(0, 0, 0, 10%); border-right: 3px solid rgba(0, 0, 0, 10%)");
        gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 10%); border-top: 3px solid rgba(0, 0, 0, 10%); border-right: 3px solid rgba(0, 0, 0, 10%)");
    }
}

void AssetGridItem::noHighlight() {
    if (!selected) {
        textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
            "border-left: 3px solid rgba(0, 0, 0, 3%); border-bottom: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
        gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 3%); border-top: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
    }
}

void AssetGridItem::highlight(bool highlight) {
	selected = highlight;
	if (selected) {
		textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
			"border-left: 3px solid #3498db; border-bottom: 3px solid #3498db; border-right: 3px solid #3498db");
		gridImageLabel->setStyleSheet("border-left: 3px solid #3498db; border-top: 3px solid #3498db; border-right: 3px solid #3498db");
	}
	else {
		dimHighlight();
	}
}
