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

signals:

private:
    QLabel* version_label_;
};

#endif // VERSIONSPLASHSCREEN_H
