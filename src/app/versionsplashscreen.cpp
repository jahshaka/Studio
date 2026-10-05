#include "app/versionsplashscreen.h"
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"

#include <QProgressBar>

VersionSplashScreen::VersionSplashScreen(const QPixmap &pixmap)
    : QSplashScreen{pixmap}
{
    version_label_ = new QLabel(this);

    version_label_->setText("");
    QFont font;
    font.setPointSize(20);
    version_label_->setFont(font);
    version_label_->setStyleSheet(StyleSheet::SplashVersionLabel());
    ThemeRoles::setTone(version_label_, ThemeRoles::Tone::Normal);

    // The startup shader-build line. Its own label rather than showMessage()
    // because showMessage is already taken by the revision string at the bottom
    // left, and a progress counter that erases the build identity every 100 ms
    // is worse than either alone.
    shader_label_ = new QLabel(this);
    shader_label_->setText("");
    QFont small;
    small.setPointSize(11);
    shader_label_->setFont(small);
    shader_label_->setStyleSheet(StyleSheet::SplashShaderLabel());
    ThemeRoles::setTone(shader_label_, ThemeRoles::Tone::Muted);
    shader_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    shader_label_->hide();

    // ...and its BAR, a thin one along the bottom edge.
    shader_bar_ = new QProgressBar(this);
    shader_bar_->setTextVisible(false);
    shader_bar_->hide();
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
    if (done < 0) { shader_label_->hide(); shader_bar_->hide(); return; }

    // Bottom right, on the same baseline as the revision message on the left;
    // the bar under it, across the splash.
    shader_label_->setGeometry(0, height() - 40, width() - 14, 22);
    // A count past the last run's total (this run built more) reads as the
    // count alone rather than "80 of 74".
    const bool bounded = total > 0 && done <= total;
    shader_label_->setText(bounded
        ? tr("Compiling shaders \u2014 %1 of %2").arg(done).arg(total)
        : tr("Compiling shaders \u2014 %1").arg(done));
    shader_label_->show();
    shader_label_->raise();
    shader_bar_->setGeometry(14, height() - 14, width() - 28, 6);
    if (bounded) { shader_bar_->setRange(0, total); shader_bar_->setValue(done); }
    else shader_bar_->setRange(0, 0);   // busy: the denominator is unknown
    shader_bar_->show();
    shader_bar_->raise();
}
