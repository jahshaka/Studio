// mcp.session — THE MCP SESSION ON THE CPU (MCP-ON-HOST-1 stage 2; irisgl/engine/mcp/include/mcp/session.h says the laws).
// No device, no engine, no transport: the transport-free Session over injected host makers (a synthetic `test.*` set:
// one `reads` command, one that `changes`, one that loops on the host's keep-going hook, one that prints) and an
// injected poll (the lines that arrive while a request runs).
// THE LAWS: server/discover's fields; _meta required (-32602); an unsupported revision -32022 with data; initialize an
// error naming 2026-07-28; ping -32601; tools/list the five in order with ttlMs / cacheScope; resultType on every
// result; an unknown tool -32602; an array -32600; garbage and an over-long line -32700 with no id; a notification
// unanswered; call / read / run_script answers equal their recorded text; read of a `changes` command an error result by
// name; a faulted registry an error result naming the faults; the engine host made ONCE, at the first engine-reaching
// tools/call (never by discover / tools/list / api_docs); a fresh realm per run_script; a cancel of the running request:
// no line for it, no progress after, the next request served; a cancel of a queued one: dropped unanswered; progress
// increasing, none after the reply. THE TRANSPORT (mcp/stdio.h): a line far past the cap is delivered `overlong` while the
// reader's own counter of bytes held never passes kLineCap; and in a FORKED server whose command printf()s, every line
// on the protocol fd is one JSON-RPC message and stdin's end exits 0 - the control: the same child WITHOUT the fd claim
// reads foreign lines there.
#include "jahshaka/engine/Script.h"
#include "jahshaka/engine/ScriptJson.h"
#include "mcp/session.h"
#include "mcp/stdio.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using namespace jahshaka::engine::script;

static int gFail = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    if (!ok) ++gFail;
}

static const char *kMeta = "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\","
                           "\"io.modelcontextprotocol/clientCapabilities\":{}}";
/// A request line: `params` without its braces, the _meta appended.
static std::string req(const std::string &id, const std::string &method, const std::string &params = "")
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"method\":\"" + method + "\",\"params\":{" + params +
           (params.empty() ? "" : ",") + kMeta + "}}";
}
static std::string toolCall(const std::string &id, const std::string &tool, const std::string &args,
                            const std::string &progressToken = "")
{
    std::string meta = kMeta;
    if (!progressToken.empty()) meta.insert(meta.size() - 1, ",\"progressToken\":" + progressToken);
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" + tool +
           "\",\"arguments\":" + args + "," + meta + "}}";
}

/// One session over the synthetic hosts, every line it sends kept (text and parsed).
struct Rig {
    int docsMade = 0, engineMade = 0, polls = 0;
    bool faulty = false;
    std::vector<std::string> sent, logged;
    std::vector<Value> parsed;
    // THE POLL: at poll number `at`, these lines arrive; `eofAt` > 0: stdin ends at that poll
    std::vector<std::pair<int, std::string>> arriving;
    int eofAt = 0;
    std::unique_ptr<mcp::Session> s;

    Rig()
    {
        mcp::Session::Doors d;
        d.makeHost = [this](const Host::Options &o, bool engine) {
            ++(engine ? engineMade : docsMade);
            auto h = std::make_unique<Host>(o);
            Host *hp = h.get();
            Registry &r = h->registry();
            r.add({"test.read", {Access::Reads, {}, Kind::Int}, "Reads seven."}, [](Call &) { return Value(7); });
            r.add({"test.set", {Access::Changes, {arg("v", Kind::Int)}, Kind::Int}, "Sets v; answers it."},
                  [](Call &c) { return c[0]; });
            r.add({"test.loop", {Access::Changes, {arg("n", Kind::Int)}, Kind::Int}, "Loops n steps on keep-going."},
                  [hp](Call &c) -> Value {
                      const std::int64_t n = c[0].asInt();
                      for (std::int64_t i = 0; i < n; ++i)
                          if (!hp->keepGoing(double(i), double(n)))
                              return c.fail("cancelled at frame " + std::to_string(i) + " of " + std::to_string(n));
                      hp->keepGoing(double(n), double(n));
                      return Value(n);
                  });
            r.add({"test.say", {Access::Reads, {}, Kind::Null}, "Prints a line."}, [hp](Call &) {
                hp->print("said");
                return Value();
            });
            if (faulty) r.add({"test.bare", {Access::Reads, {}, Kind::Null}, ""}, [](Call &) { return Value(); });
            return h;
        };
        d.send = [this](const std::string &line) {
            sent.push_back(line);
            Value v;
            std::string why;
            parseJson(line, v, why);
            parsed.push_back(v);
        };
        d.log = [this](const std::string &line) { logged.push_back(line); };
        d.poll = [this](std::vector<mcp::Incoming> &arrived) {
            ++polls;
            for (const auto &a : arriving)
                if (a.first == polls) arrived.push_back({a.second, false});
            return !(eofAt && polls >= eofAt);
        };
        d.version = "test";
        s = std::make_unique<mcp::Session>(std::move(d));
    }
    /// The one line `in` makes (the last sent), parsed; null when it made none.
    Value one(const std::string &in)
    {
        const std::size_t before = sent.size();
        s->receive({in, false});
        return sent.size() == before ? Value() : parsed.back();
    }
};

static std::int64_t code(const Value &m) { return m["error"]["code"].kind() == Kind::Int ? m["error"]["code"].asInt() : 0; }

int main()
{
    // 1. DISCOVER, THE REVISION, THE ERRORS OF THE PROTOCOL
    {
        Rig g;
        const Value d = g.one(req("1", "server/discover"));
        const Value &r = d["result"];
        check(d["id"] == Value(1) && r["resultType"] == Value("complete") &&
                  r["supportedVersions"] == Value(Value::Array{Value("2026-07-28")}) &&
                  r["capabilities"] == Value(Value::Object{{"tools", Value(Value::Object{})}}) &&
                  r["instructions"].kind() == Kind::String && r["ttlMs"].kind() == Kind::Int &&
                  r["cacheScope"] == Value("public") &&
                  r["_meta"]["io.modelcontextprotocol/serverInfo"]["name"] == Value("jahengine"),
              "server/discover answers the revision, tools, instructions, ttlMs, cacheScope and serverInfo (" + g.sent.back() + ")");
        const Value noMeta = g.one("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}");
        const std::string future = req("3", "tools/list");
        const Value newer = g.one(future.substr(0, future.find("2026-07-28")) + "2099-01-01" +
                                  future.substr(future.find("2026-07-28") + 10));
        check(code(noMeta) == -32602 && noMeta["id"] == Value(2) && code(newer) == -32022 &&
                  newer["error"]["data"] == Value(Value::Object{{"supported", Value(Value::Array{Value("2026-07-28")})},
                                                                {"requested", Value("2099-01-01")}}),
              "a request missing _meta is -32602; an unsupported revision is -32022 with {supported, requested}");
        const Value init = g.one("{\"jsonrpc\":\"2.0\",\"id\":\"i\",\"method\":\"initialize\",\"params\":"
                                 "{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{}}}");
        const Value ping = g.one(req("4", "ping"));
        check(init["id"] == Value("i") && init["error"]["message"].asString().find("2026-07-28") != std::string::npos &&
                  code(ping) == -32601,
              "initialize is an error naming 2026-07-28 (" + init["error"]["message"].asString() + "); ping is -32601");
        const Value unknownTool = g.one(toolCall("5", "draw", "{}"));
        const Value batch = g.one("[" + req("6", "tools/list") + "]");
        const Value garbage = g.one("{not json");
        const Value overlong = [&] {
            g.s->receive({"{\"jsonrpc\":", true});
            return g.parsed.back();
        }();
        check(code(unknownTool) == -32602 && unknownTool["error"]["message"] == Value("Unknown tool: draw") &&
                  code(batch) == -32600 && !batch.find("id") && code(garbage) == -32700 && !garbage.find("id") &&
                  code(overlong) == -32700 && !overlong.find("id"),
              "an unknown tool is -32602 'Unknown tool: draw'; an array -32600; garbage and an over-long line -32700, "
              "each with no id");
        const std::size_t before = g.sent.size();
        g.s->receive({"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", false});
        g.s->receive({"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":99}}", false});
        check(g.sent.size() == before, "a notification - unknown, or a cancel of no request - is never answered");
        const Value list = g.one(req("7", "tools/list"));
        std::vector<std::string> names;
        for (const Value &t : list["result"]["tools"].asArray()) names.push_back(t["name"].asString());
        check(names == std::vector<std::string>{"api_docs", "call", "read", "run_script", "picture"} &&
                  list["result"]["ttlMs"].kind() == Kind::Int && list["result"]["cacheScope"] == Value("public") &&
                  !list["result"].find("nextCursor") && list["result"]["resultType"] == Value("complete"),
              "tools/list is exactly the five, in order, with ttlMs and cacheScope and no nextCursor");
        check(g.docsMade == 0 && g.engineMade == 0, "discover and tools/list make no host");
        const Value docs = g.one(toolCall("8", "api_docs", "{\"command\":\"test.set\"}"));
        const Value &entry = docs["result"]["structuredContent"]["commands"].asArray().at(0);
        check(g.docsMade == 1 && g.engineMade == 0 && entry["access"] == Value("changes") &&
                  entry["signature"].asString().find("test.set(") == 0 && entry["inputSchema"]["type"] == Value("object") &&
                  !docs["result"].find("isError"),
              "api_docs answers from a documentation-only host: the engine host is NOT made (" + g.sent.back().substr(0, 160) + ")");
        bool everyResult = true;
        for (const Value &m : g.parsed)
            if (m.find("result")) everyResult = everyResult && m["result"]["resultType"] == Value("complete");
        check(everyResult, "every result carries resultType: complete");
    }

    // 2. THE TOOLS' ANSWERS, BY THEIR RECORDED TEXT; THE ONE ENGINE HOST; A FRESH REALM; read's LAW
    {
        Rig g;
        g.one(req("1", "tools/list"));
        const Value set = g.one(toolCall("2", "call", "{\"command\":\"test.set\",\"args\":{\"v\":5}}"));
        check(g.sent.back() == "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"resultType\":\"complete\",\"content\":[{\"type\":"
                               "\"text\",\"text\":\"{\\\"status\\\":\\\"ok\\\",\\\"value\\\":5}\"}],\"structuredContent\":"
                               "{\"status\":\"ok\",\"value\":5}}}" &&
                  g.engineMade == 1,
              "call test.set {v: 5} answers its recorded text, and the first tools/call made the engine host: " + g.sent.back());
        g.one(toolCall("3", "read", "{\"command\":\"test.read\"}"));
        check(g.sent.back() == "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"resultType\":\"complete\",\"content\":[{\"type\":"
                               "\"text\",\"text\":\"{\\\"status\\\":\\\"ok\\\",\\\"value\\\":7}\"}],\"structuredContent\":"
                               "{\"status\":\"ok\",\"value\":7}}}",
              "read test.read answers its recorded text: " + g.sent.back());
        g.one(toolCall("4", "run_script", "{\"script\":\"test.say(); 1 + 1\"}"));
        check(g.sent.back() == "{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":{\"resultType\":\"complete\",\"content\":[{\"type\":"
                               "\"text\",\"text\":\"{\\\"ok\\\":true,\\\"result\\\":2,\\\"console\\\":[\\\"said\\\"]}\"}],"
                               "\"structuredContent\":{\"ok\":true,\"result\":2,\"console\":[\"said\"]}}}",
              "run_script answers {ok, result, console} - the completion value and what the script printed: " + g.sent.back());
        const Value readChanges = g.one(toolCall("5", "read", "{\"command\":\"test.set\",\"args\":{\"v\":1}}"));
        const Value badCommand = g.one(toolCall("6", "call", "{\"command\":\"test.nope\"}"));
        check(readChanges["result"]["isError"] == Value(true) &&
                  readChanges["result"]["structuredContent"]["error"] ==
                      Value("read: test.set is not a reads command (its access: changes): use call") &&
                  badCommand["result"]["isError"] == Value(true) &&
                  badCommand["result"]["structuredContent"]["status"] == Value("failed"),
              "read of a `changes` command is an error result by name; an unknown command inside call an error result");
        g.one(toolCall("7", "run_script", "{\"script\":\"globalThis.kept = 1; 0\"}"));
        const Value second = g.one(toolCall("8", "run_script", "{\"script\":\"typeof kept\"}"));
        g.one(toolCall("9", "api_docs", "{}"));
        check(second["result"]["structuredContent"]["result"] == Value("undefined") && g.engineMade == 1 && g.docsMade == 0,
              "a FRESH realm per run_script (a global of the last call is undefined); the engine host made ONCE, and "
              "api_docs after it reads that host (no docs host made)");
        const Value pic = g.one(toolCall("10", "picture", "{\"pictures\":[1]}"));
        check(pic["result"]["isError"] == Value(true), "picture is an error result until stage 3");
    }
    {
        Rig g;
        g.faulty = true;
        const Value r = g.one(toolCall("1", "call", "{\"command\":\"test.read\"}"));
        check(r["result"]["isError"] == Value(true) &&
                  r["result"]["structuredContent"]["message"].asString().find("test.bare: no doc") != std::string::npos,
              "a faulted registry: every call is an error result naming the faults");
    }

    // 3. CANCEL AND PROGRESS
    {
        Rig g;
        g.one(toolCall("0", "call", "{\"command\":\"test.read\"}"));   // the engine host
        g.polls = 0;
        g.arriving = {{100, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":1}}"},
                      {100, toolCall("2", "read", "{\"command\":\"test.read\"}")}};
        const std::size_t before = g.sent.size();
        g.s->receive({toolCall("1", "call", "{\"command\":\"test.loop\",\"args\":{\"n\":1000}}", "\"t\""), false});
        int progress = 0, forOne = 0, last = -1;
        bool increasing = true, afterCancel = false;
        std::size_t replyTwo = 0;
        for (std::size_t i = before; i < g.parsed.size(); ++i) {
            const Value &m = g.parsed[i];
            if (m["method"] == Value("notifications/progress")) {
                ++progress;
                const int p = int(m["params"]["progress"].asInt());
                increasing = increasing && p > last;
                afterCancel = afterCancel || p >= 100;
                last = p;
            }
            if (m["id"] == Value(1)) ++forOne;
            if (m["id"] == Value(2)) replyTwo = i;
        }
        check(progress > 0 && increasing && !afterCancel && forOne == 0 && replyTwo > 0 &&
                  g.parsed[replyTwo]["result"]["structuredContent"]["value"] == Value(7),
              "a cancel of the RUNNING request: " + std::to_string(progress) + " progress lines (> 0, increasing, every "
              "one before the cancel), no line for its id, and the request behind it served");
        bool logged = false;
        for (const std::string &l : g.logged) logged = logged || l.find("cancelled at frame 99 of 1000") != std::string::npos;
        check(logged, "the cancel is said on stderr: cancelled at frame 99 of 1000");

        g.polls = 0;
        g.arriving = {{10, toolCall("21", "read", "{\"command\":\"test.read\"}")},
                      {20, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":21}}"}};
        const std::size_t mark = g.sent.size();
        g.s->receive({toolCall("20", "call", "{\"command\":\"test.loop\",\"args\":{\"n\":50}}", "7"), false});
        int twenty = 0, twentyOne = 0, progressAfterReply = 0;
        bool replied = false;
        for (std::size_t i = mark; i < g.parsed.size(); ++i) {
            const Value &m = g.parsed[i];
            if (m["id"] == Value(20)) ++twenty, replied = true;
            if (m["id"] == Value(21)) ++twentyOne;
            if (replied && m["method"] == Value("notifications/progress")) ++progressAfterReply;
        }
        check(twenty == 1 && twentyOne == 0 && progressAfterReply == 0,
              "a cancel of a QUEUED request drops it unanswered; the running one replies once; no progress after its reply");

        g.polls = 0;
        g.arriving.clear();
        g.eofAt = 5;
        const std::size_t atEof = g.sent.size();
        g.s->receive({toolCall("30", "call", "{\"command\":\"test.loop\",\"args\":{\"n\":1000}}"), false});
        check(g.s->ended() && g.sent.size() == atEof, "stdin's end mid-call cancels it (no line) and ends the session");
    }

    // 4. THE TRANSPORT
    {
        // (a) A LINE FAR PAST THE CAP: 10 x kLineCap bytes and then a request, from a writer child through a pipe
        int fds[2];
        check(::pipe(fds) == 0, "a pipe");
        std::fflush(nullptr);
        const pid_t writer = ::fork();
        if (writer == 0) {
            ::close(fds[0]);
            const std::string chunk(1 << 20, 'x');
            for (std::size_t sent = 0; sent < 10 * mcp::kLineCap; sent += chunk.size())
                if (::write(fds[1], chunk.data(), chunk.size()) < 0) ::_exit(1);
            const std::string tail = "\n" + req("1", "tools/list") + "\n";
            ::_exit(::write(fds[1], tail.data(), tail.size()) < 0 ? 1 : 0);
        }
        ::close(fds[1]);
        mcp::LineReader reader(fds[0]);
        std::vector<mcp::Incoming> lines;
        while (reader.read(lines, true)) {}
        ::close(fds[0]);
        int status = 0;
        ::waitpid(writer, &status, 0);
        check(lines.size() == 2 && lines[0].overlong && lines[0].line.empty() && !lines[1].overlong &&
                  lines[1].line == req("1", "tools/list") && reader.maxBuffered() <= mcp::kLineCap,
              "a line of " + std::to_string(10 * mcp::kLineCap) + " bytes is delivered OVERLONG and the next line whole; "
              "the reader's own counter of bytes held peaked at " + std::to_string(reader.maxBuffered()) + " (<= " +
                  std::to_string(mcp::kLineCap) + "): the discarded bytes were never kept");

        // (b) A FORKED SERVER whose command printf()s: with the fd claim, and without it (the control)
        const auto serve = [](bool claim, std::vector<std::string> &out) {
            int in[2], outPipe[2];
            if (::pipe(in) || ::pipe(outPipe)) return -1;
            std::fflush(nullptr);
            const pid_t child = ::fork();
            if (child == 0) {
                ::dup2(in[0], 0);
                ::dup2(outPipe[1], 1);
                const int devnull = ::open("/dev/null", O_WRONLY);
                ::dup2(devnull, 2);
                ::close(in[0]), ::close(in[1]), ::close(outPipe[0]), ::close(outPipe[1]), ::close(devnull);
                std::string why;
                const int proto = claim ? mcp::claimProtocolFd(why) : 1;
                mcp::Session::Doors d;
                d.version = "test";
                d.makeHost = [](const Host::Options &o, bool) {
                    auto h = std::make_unique<Host>(o);
                    h->registry().add({"test.printf", {Access::Reads, {}, Kind::Null}, "printf()s to stdout."}, [](Call &) {
                        std::printf("a foreign line on stdout\n");
                        std::fflush(stdout);
                        return Value();
                    });
                    return h;
                };
                mcp::serveStdio(0, proto, std::move(d));
                std::fflush(nullptr);
                ::_exit(0);
            }
            ::close(in[0]);
            ::close(outPipe[1]);
            const std::string requests = req("1", "server/discover") + "\n" +
                                         toolCall("2", "call", "{\"command\":\"test.printf\"}") + "\n" +
                                         req("3", "tools/list") + "\n";
            const bool wrote = ::write(in[1], requests.data(), requests.size()) == ssize_t(requests.size());
            ::close(in[1]);   // stdin's end
            mcp::LineReader r(outPipe[0]);
            std::vector<mcp::Incoming> got;
            while (r.read(got, true)) {}
            ::close(outPipe[0]);
            for (const mcp::Incoming &l : got) out.push_back(l.line);
            int status = 0;
            ::waitpid(child, &status, 0);
            return wrote && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        };
        const auto foreign = [](const std::vector<std::string> &lines) {
            int n = 0;
            for (const std::string &l : lines) {
                Value m;
                std::string why;
                n += !(parseJson(l, m, why) && m.kind() == Kind::Object && m["jsonrpc"] == Value("2.0"));
            }
            return n;
        };
        std::vector<std::string> claimed, unclaimed;
        const int claimedExit = serve(true, claimed), unclaimedExit = serve(false, unclaimed);
        check(claimedExit == 0 && claimed.size() == 3 && foreign(claimed) == 0,
              "a forked server WITH the fd claim: " + std::to_string(claimed.size()) + " lines on the protocol fd, " +
                  std::to_string(foreign(claimed)) + " foreign (the command's printf went to stderr); stdin's end exit " +
                  std::to_string(claimedExit));
        check(unclaimedExit == 0 && foreign(unclaimed) > 0,
              "the control: the same child WITHOUT the claim puts " + std::to_string(foreign(unclaimed)) +
                  " foreign line(s) on the protocol fd (> 0)");
    }

    if (gFail) std::printf("mcp.session: FAILED (%d)\n", gFail);
    else std::printf("mcp.session: PASSED\n");
    return gFail ? 1 : 0;
}
