#include "app/versionsplashscreen.h"

VersionSplashScreen::VersionSplashScreen(const QPixmap &pixmap)
    : QSplashScreen{pixmap}
{
    version_label_ = new QLabel(this);

    version_label_->setText("");
    QFont font;
    font.setPointSize(20);
    version_label_->setFont(font);
    version_label_->setStyleSheet("color: white;");

    // The startup shader-build line. Its own label rather than showMessage()
    // because showMessage is already taken by the revision string at the bottom
    // left, and a progress counter that erases the build identity every 100 ms
    // is worse than either alone.
    shader_label_ = new QLabel(this);
    shader_label_->setText("");
    QFont small;
    small.setPointSize(11);
    shader_label_->setFont(small);
    shader_label_->setStyleSheet("color: rgba(255,255,255,200);");
    shader_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    shader_label_->hide();
}

VersionSplashScreen::~VersionSplashScreen()
{

}

void VersionSplashScreen::updateVersion(const QString &version)
{
    version_label_->setGeometry(12, 5, width(), 48);

    version_label_->setText(version);

}

void VersionSplashScreen::showShaderBuild(int done, int total)
{
    if (done < 0) { shader_label_->hide(); return; }

    // Bottom right, on the same baseline as the revision message on the left.
    shader_label_->setGeometry(0, height() - 34, width() - 14, 22);
    shader_label_->setText(total > 0
        ? tr("Building shaders %1/%2").arg(done).arg(total)
        : tr("Building shaders %1").arg(done));
    shader_label_->show();
    shader_label_->raise();
}
