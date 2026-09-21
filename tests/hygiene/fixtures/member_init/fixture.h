// The guard for source.member_init's own scanner (UNINIT-SWEEP-1), in the shape
// gate_scope_rules.py guards gate-scope.py: a gate whose scanner silently stopped
// flagging would pass every day. This file is NEVER COMPILED and is outside the
// gate's own scope (tests/**) — it is read only by `member_init.py --self-test`,
// which asserts the scanner flags EXACTLY the ten members below marked FLAGGED and
// none of the nine marked COMPLIANT.
#pragma once

#include <atomic>

class QWidget;
class QString;

namespace fixture {

enum class Mode { Off, On };

// --- the ten the scanner must flag -----------------------------------------------
class Flagged {
public:
    Flagged() {}                 // a constructor that writes none of them
private:
    int count;                   // FLAGGED scalar
    QWidget *owner;              // FLAGGED pointer
    Mode mode;                   // FLAGGED enum
    int grid[4];                 // FLAGGED C array of scalars
    std::atomic<bool> busy;      // FLAGGED atomic
};

class SomeCtorsMiss {
public:
    SomeCtorsMiss() : b(false) {}
    SomeCtorsMiss(int) {}        // this one forgets it — the whole point of the rule
private:
    bool b;                      // FLAGGED covered by SOME constructors only
};

struct NoCtorAggregate {
    int x;                       // FLAGGED no constructor at all
    const char *p;               // FLAGGED
};

class DefaultedOnly {
public:
    DefaultedOnly() = default;   // `= default` writes nothing
private:
    int n;                       // FLAGGED
};

class Outer {
    class Nested {
        int deep;                // FLAGGED a nested class answers for itself
    };
};

// --- the nine it must NOT flag ---------------------------------------------------
class Compliant {
    int a = 0;                   // COMPLIANT default member initialiser
    QWidget *p{nullptr};         // COMPLIANT brace form
};

class CtorInit {
public:
    CtorInit() : x(1) {}
private:
    int x;                       // COMPLIANT the one constructor names it
};

class CtorBody {
public:
    CtorBody() { x = 1; }
private:
    int x;                       // COMPLIANT assigned in the body
};

class Delegating {
public:
    Delegating() : Delegating(0) {}
    Delegating(int v) : x(v) {}
private:
    int x;                       // COMPLIANT a delegating constructor counts as its target
};

class AllCtors {
public:
    AllCtors() : v(0) {}
    AllCtors(int n) { v = n; }
private:
    int v;                       // COMPLIANT every constructor writes it
};

struct NotOurs {
    QString str;                 // COMPLIANT a class type has its own constructor
};

struct Ref {
    Ref(int &i) : r(i) {}
    int &r;                      // COMPLIANT a reference cannot carry a DMI
};

struct Statics {
    static int s;                // COMPLIANT a static is not a member of an object
};

}   // namespace fixture
