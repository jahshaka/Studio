// core.json_qt_parity - THE ONE CODEC WRITES QT's TEXT (NOQT-CORE-1 stage 3; the lead's OPEN CALL 2: option B). This row's
// process LINKS QtCore on purpose: QJsonDocument is the reference, run beside iris::json in one process, so the comparison is
// the two writers on the same input, never a pinned copy of Qt's output. It lives until the saved scene's format is
// rewritten (V2 plan 0.16), which deletes it with Qt's writer. THE LAWS:
//   1. NUMBER EDGES: for each number of a fixed table (-0.0, 0.1, 0.1 + 0.2, 1e16, 1e-4, 0.001, 2^53, 2^53 + 1 as a double,
//      an integral double just above 2^53, 12345678901234567890, the largest and the smallest finite doubles both signs,
//      the smallest normal, the smallest subnormal, 1e21, 1e22, 2^53 + 1 and INT64_MIN as integers) iris::json::write and
//      writePretty equal QJsonDocument's Compact and Indented text of the same one-element array, byte for byte.
//   2. NaN and the two infinities: Qt writes `null` (measured here and printed); iris::json REFUSES them by name, naming
//      the value's path - a silent null would change a number into nothing (the codec's own law: core/json.h).
//   3. THE EMPTY CONTAINERS, nested (an empty array and object at the top, in an object, in an array): Indented and
//      Compact equal Qt's (Compact with the members in name order, as Qt keeps them).
//   4. THE LIBRARY's BLOBS: every JSON blob file named on the command line (json_qt_parity.py extracts them from the seeded
//      template library: its 17 assets' scene-node blobs and their 17 properties, each written by Qt's Indented writer)
//      parsed by both and re-written by both Indented: equal, byte for byte; each difference printed. THE CONTROL: the
//      blobs compared == the files given > 0. (The 21 shipped samples' 3.3 MB of scene JSON are proved by NOQT-DOCUMENT-1's
//      row samples.resave_bytes when that lane's stage 4 moves the document's JSON onto this codec: 34 library blobs
//      exercise few number shapes, which is why the table of law 1 is here.)
// usage: test_core_json_qt_parity <blob file>...
#include "irisgl/core/json.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

static int gFail = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    std::fflush(stdout);
    if (!ok) ++gFail;
}

static std::string qt(const QJsonDocument &d, QJsonDocument::JsonFormat f) { return d.toJson(f).toStdString(); }

static std::string ours(const iris::Value &v, bool pretty, std::string &why)
{
    std::string out;
    why.clear();
    const bool ok = pretty ? iris::json::writePretty(v, out, why) : iris::json::write(v, out, why);
    return ok ? out : "<refused: " + why + ">";
}

int main(int argc, char **argv)
{
    std::string why;
    // 1. THE NUMBER EDGES
    const double p53 = 9007199254740992.0;
    const double numbers[] = {0.0, -0.0, 0.1, 0.1 + 0.2, 1.0, -1.0, 0.5, 1e16, -1e16, 1e-4, 0.001, 1.25e-4, 2.5e-5, p53, -p53,
                              p53 + 1.0 /* rounds to 2^53 */, p53 + 2.0, 1.5e17, 12345678901234567890.0, 1e21, 1e22, 1e300,
                              std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(),
                              -std::numeric_limits<double>::denorm_min(), 0.05000000074505806, 128.0, 1234567.0, 3.14159,
                              // THE TIE ON BOTH SIDES (the audit's F2): a POSITIVE-exponent tie (22 bytes fixed and exponent
                              // alike) and a NEGATIVE value at a tie (9 bytes either way)
                              1.2345678901234568e21, -0.000125};
    int compared = 0, equal = 0;
    for (double d : numbers) {
        QJsonArray qa;
        qa.append(d);
        const iris::Value v(iris::Value::Array{iris::Value(d)});
        const std::string qc = qt(QJsonDocument(qa), QJsonDocument::Compact), qi = qt(QJsonDocument(qa), QJsonDocument::Indented);
        const std::string oc = ours(v, false, why), oi = ours(v, true, why);
        ++compared;
        if (qc == oc && qi == oi) {
            ++equal;
        } else {
            char b[40];
            std::snprintf(b, sizeof b, "%.17g", d);
            std::printf("   DIFFERS %s: Qt %s / ours %s (Indented: Qt %zu bytes, ours %zu)\n", b, qc.c_str(), oc.c_str(),
                        qi.size(), oi.size());
        }
    }
    QJsonArray qints;
    qints.append(qint64((std::int64_t(1) << 53) + 1));
    qints.append(qint64(std::numeric_limits<std::int64_t>::min()));
    const iris::Value ints(iris::Value::Array{iris::Value((std::int64_t(1) << 53) + 1),
                                              iris::Value(std::numeric_limits<std::int64_t>::min())});
    ++compared;
    const bool intsEqual = qt(QJsonDocument(qints), QJsonDocument::Compact) == ours(ints, false, why) &&
                           qt(QJsonDocument(qints), QJsonDocument::Indented) == ours(ints, true, why);
    equal += intsEqual;
    if (!intsEqual) std::printf("   DIFFERS the integers: Qt %s / ours %s\n", qt(QJsonDocument(qints), QJsonDocument::Compact).c_str(),
                                ours(ints, false, why).c_str());
    check(compared > 0 && equal == compared,
          "1. NUMBER EDGES: " + std::to_string(equal) + " of " + std::to_string(compared) +
              " written as Qt writes them, Compact and Indented (" + ours(iris::Value(iris::Value::Array{iris::Value(1e16),
              iris::Value(1e-4), iris::Value(-0.0), iris::Value(p53 + 2.0)}), false, why) + ")");

    // 2. NaN AND THE INFINITIES: Qt writes null; ours refuses by name
    const double bad[] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity()};
    int refused = 0;
    std::string qtText;
    for (double d : bad) {
        QJsonArray qa;
        qa.append(d);
        qtText += qt(QJsonDocument(qa), QJsonDocument::Compact) + " ";
        std::string out;
        why.clear();
        refused += !iris::json::write(iris::Value(iris::Value::Array{iris::Value(d)}), out, why) && out.empty() &&
                   why.find("value[0] is not a finite number") != std::string::npos;
    }
    check(refused == 3, "2. NaN, +inf and -inf: Qt writes " + qtText + "- ours REFUSES all " + std::to_string(refused) +
                            " of 3 by name (" + why + ")");

    // 3. THE EMPTY CONTAINERS, nested
    QJsonObject qo;
    qo["a"] = QJsonArray();
    qo["b"] = QJsonObject();
    QJsonArray qinner;
    qinner.append(QJsonArray());
    qinner.append(QJsonObject());
    qo["c"] = qinner;
    QJsonObject qd;
    qd["e"] = QJsonArray();
    qo["d"] = qd;
    const iris::Value empties(iris::Value::Object{
        {"a", iris::Value(iris::Value::Array{})}, {"b", iris::Value(iris::Value::Object{})},
        {"c", iris::Value(iris::Value::Array{iris::Value(iris::Value::Array{}), iris::Value(iris::Value::Object{})})},
        {"d", iris::Value(iris::Value::Object{{"e", iris::Value(iris::Value::Array{})}})}});
    const std::string qi = qt(QJsonDocument(qo), QJsonDocument::Indented), oi = ours(empties, true, why);
    const std::string qc = qt(QJsonDocument(qo), QJsonDocument::Compact), oc = ours(empties, false, why);
    const std::string qTopA = qt(QJsonDocument(QJsonArray()), QJsonDocument::Indented),
                      oTopA = ours(iris::Value(iris::Value::Array{}), true, why);
    const std::string qTopO = qt(QJsonDocument(QJsonObject()), QJsonDocument::Indented),
                      oTopO = ours(iris::Value(iris::Value::Object{}), true, why);
    if (qi != oi) std::printf("   DIFFERS Indented:\n--- Qt\n%s--- ours\n%s---\n", qi.c_str(), oi.c_str());
    if (qc != oc) std::printf("   DIFFERS Compact: Qt %s / ours %s\n", qc.c_str(), oc.c_str());
    check(qi == oi && qc == oc && qTopA == oTopA && qTopO == oTopO,
          "3. the empty containers, nested and at the top, Indented and Compact, as Qt writes them (" + oc + ")");

    // 3b. THE MEMBERS' ORDER BEYOND ASCII (the audit's worth-a-look 4): Qt sorts by UTF-16 code units, so an astral character
    // (U+1F600: the surrogate D83D) sorts BEFORE U+FF01 though its UTF-8 bytes (F0..) sort after (EF..): ours must follow
    const std::string zKey = "z", eAcute = "\xc3\xa9", fullwidth = "\xef\xbc\x81", astral = "\xf0\x9f\x98\x80";
    QJsonObject qn;
    for (const std::string &k : {zKey, eAcute, fullwidth, astral}) qn[QString::fromStdString(k)] = 1;
    const iris::Value names(iris::Value::Object{{fullwidth, iris::Value(1)}, {astral, iris::Value(1)}, {zKey, iris::Value(1)},
                                                {eAcute, iris::Value(1)}});
    const std::string qnI = qt(QJsonDocument(qn), QJsonDocument::Indented), onI = ours(names, true, why);
    if (qnI != onI) std::printf("   DIFFERS the member order:\n--- Qt\n%s--- ours\n%s---\n", qnI.c_str(), onI.c_str());
    check(qnI == onI && onI.find(astral) < onI.find(fullwidth),
          "3b. members named beyond ASCII (z, \xc3\xa9, U+FF01, U+1F600) are written in Qt's UTF-16 order (the astral "
          "character before U+FF01)");

    // 4. THE LIBRARY's BLOBS
    int blobs = 0, same = 0;
    for (int i = 1; i < argc; ++i) {
        QFile f(QString::fromLocal8Bit(argv[i]));
        if (!f.open(QIODevice::ReadOnly)) {
            std::printf("   UNREADABLE %s\n", argv[i]);
            continue;
        }
        const QByteArray bytes = f.readAll();
        ++blobs;
        const std::string qtOut = qt(QJsonDocument::fromJson(bytes), QJsonDocument::Indented);
        iris::Value v;
        std::string ourOut;
        if (!iris::json::parse(std::string_view(bytes.constData(), std::size_t(bytes.size())), v, why)) {
            std::printf("   REFUSED %s: %s\n", argv[i], why.c_str());
            continue;
        }
        if (!iris::json::writePretty(v, ourOut, why)) {
            std::printf("   UNWRITTEN %s: %s\n", argv[i], why.c_str());
            continue;
        }
        if (qtOut == ourOut) {
            ++same;
            continue;
        }
        std::size_t at = 0;
        while (at < qtOut.size() && at < ourOut.size() && qtOut[at] == ourOut[at]) ++at;
        std::printf("   DIFFERS %s at byte %zu: Qt \"%s\" / ours \"%s\"\n", argv[i], at, qtOut.substr(at, 40).c_str(),
                    ourOut.substr(at, 40).c_str());
    }
    check(blobs > 0 && blobs == argc - 1 && same == blobs,
          "4. the seeded library's JSON blobs: " + std::to_string(same) + " of " + std::to_string(blobs) +
              " re-written by ours exactly as Qt re-writes them (THE CONTROL: blobs compared > 0, every file given read)");

    std::printf(gFail ? "test_core_json_qt_parity: FAILED (%d)\n" : "test_core_json_qt_parity: PASSED\n", gFail);
    return gFail ? 1 : 0;
}
