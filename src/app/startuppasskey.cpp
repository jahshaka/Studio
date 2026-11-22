#include "app/startuppasskey.h"

namespace startuppass {

QString passKey(const QString &passCodeId, const QString &mediaDigest, const QString &fingerprint)
{
    return QStringLiteral("pass=") + passCodeId + QStringLiteral("|media=") + mediaDigest +
           QStringLiteral("|cache=") + fingerprint;
}

}   // namespace startuppass
