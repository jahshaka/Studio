#include "app/startuppasskey.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStringList>

#include <algorithm>

namespace startuppass {

QString mediaDigest(const QString &dir)
{
    const QDir root(dir);
    if (dir.isEmpty() || !root.exists()) return QStringLiteral("none");
    QStringList files;
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) files << root.relativeFilePath(it.next());
    std::sort(files.begin(), files.end());
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (const QString &rel : files) {
        QFile f(root.filePath(rel));
        if (!f.open(QIODevice::ReadOnly)) continue;
        h.addData(rel.toUtf8());
        h.addData(QByteArrayView("\n"));
        QCryptographicHash fh(QCryptographicHash::Sha256);
        fh.addData(&f);
        h.addData(fh.result());
    }
    return QString::fromLatin1(h.result().toHex());
}

QString passKey(const QString &passCodeId, const QString &mediaDigest, const QString &fingerprint)
{
    return QStringLiteral("pass=") + passCodeId + QStringLiteral("|media=") + mediaDigest +
           QStringLiteral("|cache=") + fingerprint;
}

}   // namespace startuppass
