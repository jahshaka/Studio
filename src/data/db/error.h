/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATA_DB_ERROR_H
#define DATA_DB_ERROR_H

// THE LIBRARY's SQLITE LAYER (NOQT-DATABASE-1): std only, the one place that calls sqlite3_*.
// A failed call answers with a NAMED error: the connection, SQLite's message and code, the
// statement, and - for a busy lock - the lock's holder in this process (ruling 6).

#include <memory>
#include <string>

namespace db {

struct Error
{
    int code = 0;            ///< SQLite's EXTENDED result code; 0 = no error
    std::string message;     ///< "<connection>: <SQLite's message> (code N)", or the layer's own refusal
    std::string statement;   ///< the SQL text the error belongs to (empty for an open or a refusal of the layer)
    std::string holder;      ///< a busy error only: who holds the write lock ("<connection> (<why>), its first write: <sql>")
    /// THE LOOK (THE LAYER GUARD, connection.h): a READ that failed marks its connection, and this flag - shared with
    /// that mark - is set when the error is TESTED (the truth test below): looking is the acknowledgement. Null for
    /// every other error. The layer's own code reads `code`, never the truth test; a log line never looks.
    std::shared_ptr<bool> looked;

    /// SQLITE_BUSY (5) and its extended codes (BUSY_SNAPSHOT 517, BUSY_RECOVERY 261, BUSY_TIMEOUT 773).
    bool busy() const { return (code & 0xff) == 5; }
    /// Did it fail? Asking IS looking: a failed read's mark on its connection is acknowledged here.
    explicit operator bool() const
    {
        if (code && looked) *looked = true;
        return code != 0;
    }
};

}   // namespace db

#endif   // DATA_DB_ERROR_H
