#ifndef VERSIONSPLASHSCREEN_H
#define VERSIONSPLASHSCREEN_H

#include <QSplashScreen>
#include <QLabel>

class VersionSplashScreen : public QSplashScreen
{
    Q_OBJECT
public:
    explicit VersionSplashScreen(const QPixmap &pixmap = QPixmap());
    ~VersionSplashScreen();

    void updateVersion(const QString& version);

    /// The startup shader build, shown on the splash while it runs
    /// (SHADER_CACHE_SPEC — owner decision 2026-09-04: shader compilation at
    /// startup happens behind the launch screen, never behind the live UI).
    /// `total` is what the last saved run needed; 0 means we have never saved a
    /// cache and there is no denominator yet, so only the count is shown.
    /// Passing done < 0 hides the line again.
    void showShaderBuild(int done, int total);

signals:

private:
    QLabel* version_label_;
    QLabel* shader_label_;
};

#endif // VERSIONSPLASHSCREEN_H
