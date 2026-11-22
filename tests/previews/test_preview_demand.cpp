/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// previews.demand — THE RENDER DEMAND (services/previewdemand.h; PREVIEWS-1 stage 2, the lead's ruling) over
// fakes of its two functions: it COUNTS REQUESTS and never waits on the settle (runNow is its door).
//   (1) scrolling through 1,000 rows (a window of 20, half a window a step, settled at each) requests every
//       missing row EXACTLY once, reads only the window's guids, ONE request a turn;
//   (2) a publish alone requests nothing (never per publish: only a settle reads);
//   (3) a row is asked once until its stamp (state|key) moves: a failed render is not asked again, a
//       moved key is;
//   (4) at most `cap` in flight, the most wanted first; a row that scrolls out before its turn is NEVER
//       requested;
//   (5) an empty publish reads nothing (an empty guid list would be no filter at all);
//   (6) a script run SUSPENDS it: 0 reads (the shell's read is the demand's FetchPreview* statements) and
//       0 requests during the run, pending dropped; after it a publish is read again;
//   (7) A NEWER PUBLISH BEFORE ITS SETTLE (the final read's C1): the rows waiting for the PREVIOUS screen are
//       never requested while the user scrolls on, and the new settle requests only the new screen;
//   (8) A VIEW HIDDEN AFTER ITS PUBLISH (TileCache::withdrawVisible tells the demand what is still shown:
//       here nothing): 0 further requests for its rows;
//   (9) a TRANSIENT failure (PreviewService::Outcome::transient) is asked again at the next settle, a FINAL
//       one is not until its stamp moves, and a row failing transiently never spins: one request a settle.
// No engine, no database, no display.

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <cstdio>
#include <functional>

#include "services/previewdemand.h"

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); }  \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
        std::fflush(stdout);                                                               \
    } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int kCap = 8;

    QHash<QString, QString> stamps;          // the library: guid -> "state|key" of a row needing a render
    int reads = 0, widestRead = 0, emptyReads = 0;
    QHash<QString, int> asked;
    QStringList order;
    bool hold = false;
    QVector<std::function<void(bool)>> held;
    QSet<QString> failTransient, failFinal;   // rows whose render fails (their stamp stays), and how

    PreviewDemand demand(
        [&](const QStringList &guids) {
            ++reads;
            widestRead = std::max(widestRead, int(guids.size()));
            if (guids.isEmpty()) ++emptyReads;
            QHash<QString, QString> need;
            for (const QString &g : guids)
                if (stamps.contains(g)) need.insert(g, stamps.value(g));
            return need;
        },
        [&](const QString &guid, std::function<void(bool)> done) {
            asked[guid] += 1;
            order << guid;
            if (hold) { held.append(done); return; }
            if (failTransient.contains(guid) || failFinal.contains(guid)) return done(failTransient.contains(guid));
            stamps.remove(guid);   // drawn: ok now
            done(false);
        },
        kCap);
    // The event loop's turns until nothing moves (bounded: a turn a request at most).
    const auto turns = [&] {
        for (int i = 0; i < 5000; ++i) {
            const int before = demand.requested();
            QCoreApplication::processEvents();
            if (demand.requested() == before && i > 2) break;
        }
    };
    const auto guidsOf = [](const char *prefix, int from, int count) {
        QStringList out;
        for (int i = from; i < from + count; ++i) out << QStringLiteral("%1-%2").arg(QLatin1String(prefix)).arg(i, 4, 10, QLatin1Char('0'));
        return out;
    };

    // (1) THE SCROLL THROUGH 1,000 ROWS.
    const QStringList library = guidsOf("g", 0, 1000);
    for (const QString &g : library) stamps.insert(g, QStringLiteral("missing|k1"));
    bool oneATurn = true;
    for (int top = 0; top + 20 <= 1000; top += 10) {
        demand.want(library.mid(top, 20));
        const int before = demand.requested();
        demand.runNow();
        oneATurn &= demand.requested() - before <= 1;
        turns();
    }
    int twice = 0;
    for (const QString &g : library) twice += asked.value(g) > 1 ? 1 : 0;
    CHECK(demand.requested() == 1000 && asked.size() == 1000 && twice == 0,
          "scrolling through 1,000 rows requests each missing row EXACTLY once (%d requests, %lld rows, %d twice)",
          demand.requested(), static_cast<long long>(asked.size()), twice);
    CHECK(widestRead == 20, "...reading only the window's guids, never the library (widest read %d guids)", widestRead);
    CHECK(oneATurn, "...ONE request a turn (a render is inline: the next waits for the next turn)");

    // (2) A PUBLISH ALONE REQUESTS NOTHING.
    const QStringList fresh = guidsOf("f", 0, 20);
    for (const QString &g : fresh) stamps.insert(g, QStringLiteral("missing|k1"));
    int requested = demand.requested(), readsBefore = reads;
    demand.want(fresh);
    CHECK(demand.requested() == requested && reads == readsBefore, "a publish alone reads and requests nothing");
    demand.runNow();
    turns();
    CHECK(demand.requested() - requested == 20, "...its settle requests the 20 missing (%d)", demand.requested() - requested);

    // (3) ONCE UNTIL ITS STAMP MOVES.
    requested = demand.requested();
    stamps.insert(fresh.first(), QStringLiteral("missing|k1"));   // its render "failed": the same stamp
    demand.want({ fresh.first() });
    demand.runNow();
    turns();
    CHECK(demand.requested() == requested, "a row whose render left it at the SAME state and key is not asked again");
    stamps.insert(fresh.first(), QStringLiteral("stale|k2"));      // its definition moved
    demand.want({ fresh.first() });
    demand.runNow();
    turns();
    CHECK(demand.requested() - requested == 1 && asked.value(fresh.first()) == 2,
          "...and IS when its key moves (%d request)", demand.requested() - requested);

    // (4) THE CAP; A ROW THAT SCROLLS OUT BEFORE ITS TURN.
    hold = true;
    order.clear();
    const QStringList first = guidsOf("h", 0, 20), second = guidsOf("h", 20, 20);
    for (const QString &g : first + second) stamps.insert(g, QStringLiteral("missing|k1"));
    requested = demand.requested();
    demand.want(first);
    demand.runNow();
    turns();
    CHECK(demand.requested() - requested == kCap && demand.inFlight() == kCap && order == first.mid(0, kCap),
          "at most the cap (%d) in flight, the most wanted first (%d requested)", kCap, demand.requested() - requested);
    demand.want(second);   // the view scrolled on before the rest's turn
    demand.runNow();
    turns();
    CHECK(demand.requested() - requested == kCap, "...a settle at the cap requests nothing more");
    for (int round = 0; round < 10 && !held.isEmpty(); ++round) {
        const QVector<std::function<void(bool)>> landing = held;
        held.clear();
        for (const auto &done : landing) done(false);
        turns();
    }
    int scrolledOut = 0, secondOnce = 0;
    for (const QString &g : first.mid(kCap)) scrolledOut += asked.value(g);
    for (const QString &g : second) secondOnce += asked.value(g) == 1 ? 1 : 0;
    CHECK(scrolledOut == 0, "the %d rows that scrolled out before their turn are NEVER requested (%d requests)",
          int(first.size()) - kCap, scrolledOut);
    CHECK(secondOnce == 20 && order.mid(kCap) == second, "...the rows now shown each once, in their order (%d)", secondOnce);
    hold = false;

    // (6) A SCRIPT RUN SUSPENDS IT (the lead's ruling: it serves a person looking at a view, never a
    // script): no read and no render while suspended - what was pending is dropped, a request already made
    // completes; after the run a publish is read again.
    hold = true;
    order.clear();
    const QStringList during = guidsOf("s", 0, 20);
    for (const QString &g : during) stamps.insert(g, QStringLiteral("missing|k1"));
    requested = demand.requested();
    demand.want(during);
    demand.runNow();
    turns();
    CHECK(demand.inFlight() == kCap && demand.requested() - requested == kCap, "before the run: the cap in flight (%d)",
          demand.inFlight());
    demand.setSuspended(true);   // ScriptEngine::runningChanged(true)
    readsBefore = reads;
    requested = demand.requested();
    demand.want(guidsOf("s", 20, 20));
    demand.runNow();
    for (int round = 0; round < 10 && !held.isEmpty(); ++round) {
        const QVector<std::function<void(bool)>> landing = held;
        held.clear();
        for (const auto &done : landing) done(false);
        turns();
    }
    CHECK(reads == readsBefore && demand.requested() == requested && demand.inFlight() == 0,
          "DURING A SCRIPT RUN: 0 demand reads, 0 requests (%d reads, %d requests; the %d made before completed)",
          reads - readsBefore, demand.requested() - requested, kCap);
    demand.setSuspended(false);  // the run ended: the views re-publish (TileCache::republishAll)
    hold = false;
    demand.want(during);
    demand.runNow();
    turns();
    CHECK(reads == readsBefore + 1 && demand.requested() - requested == int(during.size()) - kCap,
          "...after it, the re-publish is read and the %d never requested are (%d)", int(during.size()) - kCap,
          demand.requested() - requested);

    // (7) A NEWER PUBLISH BEFORE ITS SETTLE. Screen A settles (its first row is requested, 19 wait a turn
    // each), then the user scrolls on to screen B before those turns: while B settles NOTHING is requested,
    // and B's settle drops A's rows. Case (4) calls runNow() straight after its second want(), so it never
    // runs a turn between a publish and its settle and could not see this. The law counts requests, never
    // the settle's clock: if the box is slow enough for B's settle to fire inside the turns, A's rows are
    // still never requested.
    {
        order.clear();
        const QStringList a = guidsOf("p", 0, 20), b = guidsOf("p", 40, 20);
        for (const QString &g : a + b) stamps.insert(g, QStringLiteral("missing|k1"));
        const auto askedOf = [&](const QStringList &guids) {
            int n = 0;
            for (const QString &g : guids) n += asked.value(g);
            return n;
        };
        demand.want(a);
        demand.runNow();   // A settles
        demand.want(b);    // the scroll moved on: B's settle is pending
        turns();
        CHECK(askedOf(a.mid(1)) == 0, "A NEWER PUBLISH BEFORE ITS SETTLE: 0 of the previous screen's 19 waiting rows "
                                      "are requested while it settles (%d)", askedOf(a.mid(1)));
        demand.runNow();   // B's settle
        turns();
        CHECK(askedOf(a.mid(1)) == 0 && askedOf(b) == 20,
              "...and the new settle requests the new screen only (%d of A's waiting rows, %d of B's 20)",
              askedOf(a.mid(1)), askedOf(b));

        // (8) A VIEW HIDDEN AFTER ITS PUBLISH: the cache re-tells the demand what the shown views still want
        // (TileCache::withdrawVisible; here no view is left: an empty list) - 0 further requests for its rows.
        const QStringList shown = guidsOf("q", 0, 20);
        for (const QString &g : shown) stamps.insert(g, QStringLiteral("missing|k1"));
        demand.want(shown);
        demand.runNow();   // its first row is requested, 19 wait
        demand.want({});   // the page was hidden
        turns();
        demand.runNow();   // the empty publish's settle
        turns();
        CHECK(askedOf(shown.mid(1)) == 0, "A VIEW HIDDEN AFTER ITS PUBLISH: 0 further requests for its 19 waiting rows (%d)",
              askedOf(shown.mid(1)));
    }

    // (9) A TRANSIENT FAILURE IS ASKED AGAIN, ONCE A SETTLE (the final read's W1; the lead's ruling): a render
    // refused because the cause can change with no change to the row (PreviewService::Outcome::transient: the
    // engine not yet up, the renderer busy) is requested again at the next publish's settle; a FINAL failure
    // (an empty definition, a missing bake) is not, until its stamp moves; a row that fails transiently at
    // every settle never spins: exactly one request a settle.
    {
        const QString t = QStringLiteral("t-0000"), f = QStringLiteral("t-0001");
        failTransient.insert(t);
        failFinal.insert(f);
        stamps.insert(t, QStringLiteral("missing|k1"));
        stamps.insert(f, QStringLiteral("missing|k1"));
        demand.want({ t, f });
        demand.runNow();
        turns();
        CHECK(asked.value(t) == 1 && asked.value(f) == 1, "both failing rows are requested once at the first settle "
                                                         "(transient %d, final %d)", asked.value(t), asked.value(f));
        demand.want({ t, f });   // the next publish
        demand.runNow();
        turns();
        CHECK(asked.value(t) == 2, "A TRANSIENT FAILURE is requested again at the next publish's settle (%d requests)",
              asked.value(t));
        CHECK(asked.value(f) == 1, "...a FINAL failure is not, its stamp unmoved (%d request)", asked.value(f));
        bool oneASettle = true;
        for (int publish = 0; publish < 5; ++publish) {
            const int before = asked.value(t);
            demand.want({ t, f });
            demand.runNow();
            turns();
            oneASettle &= asked.value(t) - before == 1;
        }
        CHECK(oneASettle && asked.value(t) == 7 && asked.value(f) == 1,
              "...and a row failing transiently at every settle never spins: one request a settle (%d over 7 settles; "
              "the final row %d)", asked.value(t), asked.value(f));
        failTransient.clear();
        failFinal.clear();
    }

    // (5) AN EMPTY PUBLISH READS NOTHING.
    readsBefore = reads;
    demand.want({});
    demand.runNow();
    CHECK(reads == readsBefore && emptyReads == 0, "an empty publish reads nothing (%d empty reads)", emptyReads);

    std::printf(failures ? "previews.demand: FAILED (%d)\n" : "previews.demand: PASSED\n", failures);
    return failures ? 1 : 0;
}
