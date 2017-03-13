#ifndef HASHEDLIST_H
#define HASHEDLIST_H

#include <QHash>

// ref: http://www.qtcentre.org/threads/29190-A-Qt-collection-that-is-ordered-and-can-be-accessed-via-hash-key
// This class is designed to act as a hash that preserves insortion order
template <class K, class V> class HashedList : public QVector<V>
{
public:
    void addItem(const T &item) {
        int h = qHash(item);
        int pos = count();
        m_hash.insert(h, pos);
        append(item);
    }

    int hashedIndexOf(const T& item) const
    {
        int h = qHash(item);
        return m_hash.value(h, -1);
    }

private:
      QHash<int, int> m_hash;
};

#endif // HASHEDLIST_H
