// services.looks — the looks catalogue's two halves agree, and the document's
// validator enforces the stack's rules (SPECS/POST_LOOKS_SPEC.md §4.1).
//
// THE CATALOGUE IS SPLIT ON PURPOSE. The contract (ids, parameters, defaults,
// ranges) lives on the document, because SceneMirror and the scene reader need
// it and neither can see src/; the presentation (labels, tooltips, scrub
// sensitivity) lives in src/services/looks.h. The one thing that can rot is a
// look landing in one table and not the other, and looks::validate() is the
// single check for it — this suite is what makes forgetting a label a BUILD
// failure instead of a blank row in the World panel.
//
// The second half tests iris::normalizeLookStack, which is the ONE validator
// every write path shares: the verbs, the scene reader and the panel. Its rules
// are the whole authoring model — known ids only, one instance per look, every
// parameter present and clamped, and the array order preserved because the
// array order IS the frame order.
#include "services/looks.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf(cond ? "ok: " : "FAIL: ");                                  \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static QJsonObject entry(const QString &id, const QJsonObject &params = QJsonObject(),
                         bool enabled = true)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("enabled"), enabled);
    if (!params.isEmpty()) o.insert(QStringLiteral("params"), params);
    return o;
}

int main()
{
    // ---- the two tables agree ---------------------------------------------
    {
        const QString err = looks::validate();
        CHECK_MSG(err.isEmpty(), "the document catalogue and the presentation table agree%s%s",
                  err.isEmpty() ? "" : ": ", err.toUtf8().constData());
    }

    // ---- every look's first parameter is the amount, and 0 is inside it ----
    {
        int count = 0;
        const iris::LookDef *cat = iris::lookCatalogue(count);
        CHECK(count >= 1, "the catalogue is not empty");
        for (int i = 0; i < count; ++i) {
            const iris::LookDef &d = cat[i];
            CHECK_MSG(d.paramCount >= 1 && d.paramCount <= 8,
                      "%s has 1..8 parameters (%d)", d.id, d.paramCount);
            CHECK_MSG(QLatin1String(d.params[0].id) == QLatin1String("amount"),
                      "%s's first parameter is 'amount' (it is '%s')", d.id, d.params[0].id);
            // Every look is an exact identity at amount 0, so 0 has to be a
            // legal value for it — otherwise the identity is unreachable.
            CHECK_MSG(d.params[0].minValue <= 0.0f,
                      "%s's amount can reach 0 (min %.3f)", d.id, double(d.params[0].minValue));
            for (int j = 0; j < d.paramCount; ++j)
                CHECK_MSG(d.params[j].minValue <= d.params[j].defaultValue &&
                              d.params[j].defaultValue <= d.params[j].maxValue,
                          "%s.%s default %.3f is inside [%.3f, %.3f]", d.id, d.params[j].id,
                          double(d.params[j].defaultValue), double(d.params[j].minValue),
                          double(d.params[j].maxValue));
        }
    }

    // ---- normalizeLookStack: the rules -------------------------------------
    int count = 0;
    const iris::LookDef *cat = iris::lookCatalogue(count);
    const QString first = QString::fromLatin1(cat[0].id);
    const QString firstParam = QString::fromLatin1(cat[0].params[0].id);

    {
        const QJsonArray clean = iris::normalizeLookStack(QJsonArray());
        CHECK(clean.isEmpty(), "an empty stack normalizes to an empty stack");
    }
    {
        QJsonArray in;
        in.append(entry(QStringLiteral("no-such-look-at-all")));
        in.append(QJsonValue(42));            // not even an object
        in.append(entry(first));
        const QJsonArray clean = iris::normalizeLookStack(in);
        CHECK_MSG(clean.size() == 1, "unknown ids and non-objects are dropped (%d left)",
                  clean.size());
        CHECK(clean.at(0).toObject().value(QStringLiteral("id")).toString() == first,
              "and the known look survives");
    }
    {
        QJsonArray in;
        in.append(entry(first));
        in.append(entry(first));
        const QJsonArray clean = iris::normalizeLookStack(in);
        CHECK_MSG(clean.size() == 1, "a look may appear only once (%d left)", clean.size());
    }
    {
        // Every parameter present, defaulted, and clamped — an authored file
        // cannot inject a value the shader was not written for.
        QJsonObject params;
        params.insert(firstParam, double(cat[0].params[0].maxValue) + 1000.0);
        QJsonArray in;
        in.append(entry(first, params));
        const QJsonObject out = iris::normalizeLookStack(in).at(0).toObject();
        const QJsonObject p = out.value(QStringLiteral("params")).toObject();
        CHECK_MSG(p.size() == cat[0].paramCount,
                  "every parameter is written back (%d of %d)", p.size(), cat[0].paramCount);
        CHECK_MSG(qFuzzyCompare(float(p.value(firstParam).toDouble()) + 1.0f,
                                cat[0].params[0].maxValue + 1.0f),
                  "an over-range value is clamped to %.3f (got %.3f)",
                  double(cat[0].params[0].maxValue), p.value(firstParam).toDouble());
        CHECK(out.value(QStringLiteral("enabled")).toBool() == true,
              "enabled defaults to true");
    }
    {
        // A missing 'params' block means every default, which is what an entry
        // written by an older build says.
        QJsonArray in;
        in.append(entry(first));
        const QJsonObject p = iris::normalizeLookStack(in).at(0).toObject()
                                  .value(QStringLiteral("params")).toObject();
        CHECK_MSG(qFuzzyCompare(float(p.value(firstParam).toDouble()) + 1.0f,
                                cat[0].params[0].defaultValue + 1.0f),
                  "a missing parameter takes its default %.3f (got %.3f)",
                  double(cat[0].params[0].defaultValue), p.value(firstParam).toDouble());
    }
    {
        // ORDER IS PRESERVED — the array order is the frame order, and a
        // validator that sorted or de-duplicated by anything but first-wins
        // would silently change what a scene looks like.
        QJsonArray in;
        for (int i = count - 1; i >= 0; --i) in.append(entry(QString::fromLatin1(cat[i].id)));
        const QJsonArray clean = iris::normalizeLookStack(in);
        bool ordered = clean.size() == count;
        for (int i = 0; ordered && i < clean.size(); ++i)
            ordered = clean.at(i).toObject().value(QStringLiteral("id")).toString() ==
                      QString::fromLatin1(cat[count - 1 - i].id);
        CHECK(ordered, "the stack keeps the order it was given");
    }
    {
        // lookParamValues is what the mirror hands the engine: canonical order,
        // clamped, zero-filled to eight.
        float p[8];
        QJsonObject params;
        params.insert(firstParam, 0.0);
        iris::lookParamValues(cat[0], entry(first, params), p);
        CHECK_MSG(p[0] == 0.0f, "lookParamValues reads the authored value (%.3f)", double(p[0]));
        for (int i = cat[0].paramCount; i < 8; ++i)
            CHECK_MSG(p[i] == 0.0f, "unused slot %d is zero", i);
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
