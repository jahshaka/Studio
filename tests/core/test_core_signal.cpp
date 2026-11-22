// core.signal - ONE ANNOUNCEMENT's LAWS (NOQT-CORE-1 stage 1; core/signal.h): connect / notify / disconnect; a subscriber
// that connects or disconnects (itself or another) DURING a notify does not change THAT notify - it calls exactly the
// subscribers connected when it began, in connection order; the next notify sees the change.
#include "core/signal.h"

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

    // during a notify: c disconnects b (later in the list), connects d, and disconnects itself - THIS notify still calls
    // b and not d; the next sees neither b nor c, and d
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
    check(notified > 0, "subscribers were called");
    std::printf("%d checks, %d failures; %ld subscriber calls\n", checks, failures, notified);
    std::printf("core.signal: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
