#include "versionsplashscreen.h"

VersionSplashScreen::VersionSplashScreen(const QPixmap &pixmap)
    : QSplashScreen{pixmap}
{
    version_label_ = new QLabel(this);

    version_label_->setText("");
    QFont font;
    font.setPointSize(20);
    version_label_->setFont(font);
    version_label_->setStyleSheet("color: white;");
}

VersionSplashScreen::~VersionSplashScreen()
{

}

void VersionSplashScreen::updateVersion(const QString &version)
{
    version_label_->setGeometry(12, 5, width(), 48);

    version_label_->setText(version);

}
