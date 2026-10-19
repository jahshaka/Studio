// document.material_duplicate — A MATERIAL OWNS ITS ROWS, AND A DUPLICATE OWNS ITS OWN (LEAK-1 stage 1, debt
// LSAN-L1-MATERIAL-PROPERTIES).
//
// WHAT WAS WRONG. iris::Material held its editor-facing rows as a bare QList<Property*> that nothing ever freed: every
// material leaked its ~180 rows, and iris::PbrMaterial::createProperties led every LeakSanitizer report of the tree
// (the app alone, 5.1 MB in 23,103 allocations on a bare `--script quit`). A destructor alone could not fix it:
// duplicate() starts from a member-wise copy, which handed the SAME row pointers to the second material, so freeing
// them in ~Material would have double-freed the first time a duplicated node was deleted.
//
// WHAT IS ASSERTED (the law: the owner is the type whose copy IS the deep copy — iris::PropertyRows, each row cloned
// by Property::clone):
//   1. every row type clones to its own type with every field (value, range, labels, a texture row's asset guid);
//   2. a duplicate's rows are DISTINCT objects with EQUAL names, order and values;
//   3. destroying the ORIGINAL leaves the copy whole — its rows readable and writable, its fields intact;
//   4. and the other way round: destroying the copy leaves the original whole.
// Under the leak lane (scripts/sanitize.sh lsan) the row also proves the rows are FREED: a material that leaked them
// is this row's red. Document only: no display, no renderer.
#include <QGuiApplication>
#include <cstdio>
#include <cmath>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/materials/pbrmaterial.h"

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

namespace {

/// The rows of `m` as name -> value, in order — what a reader of the rows sees.
QList<QPair<QString, QVariant>> rowsOf(const iris::Material &m)
{
    QList<QPair<QString, QVariant>> out;
    for (iris::Property *p : m.properties()) out.append({ p->name, p->getValue() });
    return out;
}

iris::Property *rowOf(const iris::Material &m, const QString &name)
{
    for (iris::Property *p : m.properties())
        if (p->name == name) return p;
    return nullptr;
}

}   // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // ---- 1. every row type clones whole, to its own type ------------------
    {
        iris::FloatProperty f;
        f.id = 7; f.name = QStringLiteral("f"); f.displayName = QStringLiteral("F"); f.uniform = QStringLiteral("u_f");
        f.value = 0.25f; f.minValue = -1.0f; f.maxValue = 2.0f; f.readOnly = true;
        auto fc = f.clone();
        auto *fp = dynamic_cast<iris::FloatProperty *>(fc.get());
        CHECK(fp && fp != &f && fp->id == 7 && fp->name == f.name && fp->displayName == f.displayName &&
              fp->uniform == f.uniform && fp->value == 0.25f && fp->minValue == -1.0f && fp->maxValue == 2.0f &&
              fp->readOnly && fp->type == iris::PropertyType::Float,
              "clone: a float row keeps its type, value, range and flags");

        iris::TextureProperty t;
        t.name = QStringLiteral("baseColorMap");
        t.setValue(iris::Material::textureRef(QStringLiteral("/store/a.png"), QStringLiteral("guid-a")));
        t.toggleValue = QStringLiteral("on");
        auto tc = t.clone();
        auto *tp = dynamic_cast<iris::TextureProperty *>(tc.get());
        CHECK(tp && tp->value == QLatin1String("/store/a.png") && tp->assetGuid == QLatin1String("guid-a") &&
              tp->toggle && tp->toggleValue == QLatin1String("on") && tp->type == iris::PropertyType::Texture,
              "clone: a texture row keeps its file AND its asset identity (TEX-REF-1)");

        iris::ListProperty l;
        l.labels = { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") };
        l.value = 2;
        auto lc = l.clone();
        auto *lp = dynamic_cast<iris::ListProperty *>(lc.get());
        CHECK(lp && lp->labels == l.labels && lp->value == 2, "clone: an enum row keeps its vocabulary and value");

        iris::BoolProperty b; b.value = true;
        iris::IntProperty i; i.value = -3; i.minValue = -5; i.maxValue = 5;
        iris::ColorProperty c; c.value = QColor(1, 2, 3, 4);
        iris::FileProperty fi; fi.value = QStringLiteral("x.ies"); fi.suffix = QStringLiteral("ies");
        iris::Vec2Property v2; v2.value = QVector2D(1, 2);
        iris::Vec3Property v3; v3.value = QVector3D(1, 2, 3);
        iris::Vec4Property v4; v4.value = QVector4D(1, 2, 3, 4);
        bool same = true;
        for (iris::Property *p : std::initializer_list<iris::Property *>{ &b, &i, &c, &fi, &v2, &v3, &v4 }) {
            auto q = p->clone();
            if (!q || q.get() == p || q->type != p->type || q->getValue() != p->getValue()) same = false;
        }
        auto *ic = dynamic_cast<iris::IntProperty *>(i.clone().release());
        same = same && ic && ic->minValue == -5 && ic->maxValue == 5;
        delete ic;
        auto fic = fi.clone();
        same = same && static_cast<iris::FileProperty *>(fic.get())->suffix == QLatin1String("ies");
        CHECK(same, "clone: bool, int, colour, file and the three vector rows clone to their own type and value");
    }

    // ---- 2. a duplicate's rows: distinct objects, equal values -----------
    auto original = iris::PbrMaterial::create();
    original->setName(QStringLiteral("Brick"));
    original->setValue(QStringLiteral("baseColor"), QColor(200, 20, 20));
    original->setValue(QStringLiteral("roughness"), 0.7f);
    original->setValue(QStringLiteral("alphaMode"), 2);
    original->setValue(QStringLiteral("detail0Blend"), 4);
    original->setValue(QStringLiteral("normalMap"),
                       iris::Material::textureRef(QStringLiteral("/nowhere/n.png"), QStringLiteral("guid-n")));
    const auto before = rowsOf(*original);
    CHECK(!before.isEmpty() && before.size() == iris::PbrMaterial::create()->properties().size(),
          "the material carries its full row set (a fresh material's count)");

    iris::PbrMaterialPtr copy = original->duplicate().dynamicCast<iris::PbrMaterial>();
    CHECK(!!copy && copy.data() != original.data(), "duplicate: a PbrMaterial of its own");
    if (!copy) return 1;
    bool distinct = copy->properties().size() == original->properties().size();
    for (int r = 0; distinct && r < original->properties().size(); ++r)
        if (copy->properties()[r] == original->properties()[r]) distinct = false;
    CHECK(distinct, "duplicate: no row is shared — every row of the copy is its own object");
    CHECK(rowsOf(*copy) == before, "duplicate: every row is the original's, in the same order, with the same value");
    CHECK(copy->textureGuid(QStringLiteral("normalMap")) == QLatin1String("guid-n"),
          "duplicate: a map row names the same asset (the guid travels with the row)");

    // ---- 3. destroying the original leaves the copy whole ------------------
    original.reset();
    CHECK(rowsOf(*copy) == before, "original destroyed: the copy's rows are all there, values unchanged");
    copy->setValue(QStringLiteral("roughness"), 0.1f);
    iris::Property *rough = rowOf(*copy, QStringLiteral("roughness"));
    CHECK(rough && std::fabs(rough->getValue().toFloat() - 0.1f) < 1e-6f && std::fabs(copy->roughnessFactor - 0.1f) < 1e-6f,
          "original destroyed: the copy's rows still take writes (field and row in step)");

    // ---- 4. and the other way round -------------------------------------
    auto second = copy->duplicate().dynamicCast<iris::PbrMaterial>();
    const auto copyRows = rowsOf(*copy);
    copy.reset();
    CHECK(second && rowsOf(*second) == copyRows, "copy destroyed: the material it was duplicated into keeps its rows");
    if (second) {
        second->setValue(QStringLiteral("baseColor"), QColor(20, 20, 200));
        iris::Property *col = rowOf(*second, QStringLiteral("baseColor"));
        CHECK(col && col->getValue().value<QColor>() == QColor(20, 20, 200) && second->baseColor == QColor(20, 20, 200),
              "copy destroyed: the survivor's rows still take writes");
    }

    std::printf(failures == 0 ? "document.material_duplicate: ALL PASS\n"
                              : "document.material_duplicate: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
