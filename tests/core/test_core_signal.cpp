// core.signal - ONE ANNOUNCEMENT's LAWS (NOQT-CORE-1 stage 1; core/signal.h): connect / notify / disconnect; a subscriber
// that connects or disconnects (itself or another) DURING a notify does not change THAT notify - it calls exactly the
// subscribers connected when it began, in connection order; the next notify sees the change. THE LAW KEPT FROM THE LISTS IT
// REPLACES (services.h / assetservice.h at the base: "Iterate a COPY"): a subscriber DISCONNECTED by an EARLIER one in the
// same notify IS STILL CALLED by that notify - the snapshot was taken when it began - and is not called by the next.
#include "irisgl/core/signal.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
static void check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok && ++failures <= 40) std::printf("FAIL %s\n", what.c_str());
}

int main()
{
    iris::Signal<const std::string &> sig;
    std::vector<std::string> calls;
    long notified = 0;
    const auto a = sig.connect([&](const std::string &s) { calls.push_back("a:" + s); ++notified; });
    const auto b = sig.connect([&](const std::string &s) { calls.push_back("b:" + s); ++notified; });
    check(a && b && a != b && sig.size() == 2, "two connections, two ids");
    check(sig.connect(nullptr) == 0 && sig.size() == 2, "an empty function connects nothing");
    sig.notify("1");
    check(calls == std::vector<std::string>{ "a:1", "b:1" }, "notify calls each subscriber once, in order");
    check(sig.disconnect(a) && !sig.disconnect(a) && sig.size() == 1, "disconnect once, then nothing to disconnect");
    calls.clear();
    sig.notify("2");
    check(calls == std::vector<std::string>{ "b:2" }, "a disconnected subscriber is not called");

    // during a notify: c disconnects b (EARLIER in the list: already called), connects d, and disconnects itself - THIS
    // notify calls b, c and e and not d; the next sees neither b nor c, and d
    iris::Signal<const std::string &>::Id c = 0;
    c = sig.connect([&](const std::string &s) {
        calls.push_back("c:" + s);
        ++notified;
        sig.disconnect(b);
        sig.connect([&](const std::string &t) { calls.push_back("d:" + t); ++notified; });
        sig.disconnect(c);
    });
    // order now: b, c - make c run BEFORE a later subscriber e that c does not touch
    sig.connect([&](const std::string &s) { calls.push_back("e:" + s); ++notified; });
    calls.clear();
    sig.notify("3");
    check(calls == std::vector<std::string>{ "b:3", "c:3", "e:3" }, "the changes made during a notify do not change it");
    calls.clear();
    sig.notify("4");
    check(calls == std::vector<std::string>{ "e:4", "d:4" }, "the next notify sees them");

    // THE SNAPSHOT's DANGEROUS HALF: x disconnects y, LATER in the list and not yet called - THIS notify still calls y
    iris::Signal<const std::string &> two;
    iris::Signal<const std::string &>::Id y = 0;
    two.connect([&](const std::string &s) { calls.push_back("x:" + s); ++notified; two.disconnect(y); });
    y = two.connect([&](const std::string &s) { calls.push_back("y:" + s); ++notified; });
    calls.clear();
    two.notify("5");
    check(calls == std::vector<std::string>{ "x:5", "y:5" },
          "a subscriber disconnected by an EARLIER one in the same notify is still called by it (the snapshot)");
    calls.clear();
    two.notify("6");
    check(calls == std::vector<std::string>{ "x:6" } && two.size() == 1, "...and not by the next");
    check(notified > 0, "subscribers were called");
    std::printf("%d checks, %d failures; %ld subscriber calls\n", checks, failures, notified);
    std::printf("core.signal: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
