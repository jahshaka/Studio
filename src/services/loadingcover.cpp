#include "services/loadingcover.h"
#include "data/settingsmanager.h"

namespace loadingcover {

bool enabled()
{
    SettingsManager *s = SettingsManager::getDefaultManager();
    // NO MANAGER IS THE DEFAULT, not an error: a headless or stand-in session
    // has no settings file and no cover to draw either way.
    return s ? s->getValue(QString::fromLatin1(settingsKey()), false).toBool() : false;
}

void setEnabled(bool on)
{
    if (SettingsManager *s = SettingsManager::getDefaultManager())
        s->setValue(QString::fromLatin1(settingsKey()), on);
}

}   // namespace loadingcover
