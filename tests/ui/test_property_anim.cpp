/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.property_anim — the timeline's PropertyAnim factory never hands back an
// indeterminate pointer (ENGINEERING_DEBT_SPEC item 8).
//
// The bug class this guards: AnimationWidget::createPropertyAnim declared an
// UNINITIALISED `iris::PropertyAnim* anim`, switched on the property type, and
// its default branch was `Q_ASSERT(false)` — which compiles to nothing under
// QT_NO_DEBUG. The next line dereferenced the indeterminate pointer. The only
// defence was a menu filter 130 lines away. Under Clang (the macOS port) the
// optimiser is entitled to treat that path as unreachable and delete code
// around it.
//
// The fix moved the switch into makePropertyAnim() which returns nullptr
// for unsupported types. This suite exercises exactly the types that used to
// be UB — Bool/Int/String(File)/Texture/Vec2/Vec4/List/None — and asserts the
// three supported ones still produce a track of the right width.

#include <QTest>

#include "irisgl/core/properties/property.h"
#include "irisgl/document/animation/keyframeset.h"
#include "irisgl/document/animation/propertyanim.h"

#include "ui/panels/timeline/propertyanimfactory.h"

class PropertyAnimTest : public QObject
{
    Q_OBJECT

private slots:
    //! The three animatable types build a real track, named, of the right width.
    void supportedTypesBuildTracks();

    //! Every other PropertyType returns nullptr. This is the type set that was UB.
    void unsupportedTypesReturnNull();

    //! The menu filter's predicate and the factory agree, for every enum value.
    void predicateAgreesWithFactory();
};

void PropertyAnimTest::supportedTypesBuildTracks()
{
    struct Case { iris::PropertyType type; int frames; const char *name; };
    const Case cases[] = {
        { iris::PropertyType::Float, 1, "intensity" },
        { iris::PropertyType::Vec3,  3, "position"  },
        { iris::PropertyType::Color, 4, "color"     },
    };

    for (const auto &c : cases) {
        iris::PropertyAnim *anim = makePropertyAnim(c.type, QString(c.name));
        QVERIFY2(anim != nullptr, c.name);
        QCOMPARE(anim->getName(), QString(c.name));

        // The track width is what addPropertyKey() indexes: [0] for Float,
        // [0..2] for Vec3, [0..3] for Color. A short track is the other half
        // of the same crash.
        const auto frames = anim->getKeyFrames();
        QCOMPARE(frames.count(), c.frames);
        for (int i = 0; i < frames.count(); ++i)
            QVERIFY2(frames[i].keyFrame != nullptr, c.name);

        delete anim;
    }
}

void PropertyAnimTest::unsupportedTypesReturnNull()
{
    // Exactly the types the document reflects but the timeline cannot animate:
    // name/visible/lightType/meshPath and friends used to arrive here.
    const iris::PropertyType unsupported[] = {
        iris::PropertyType::None,
        iris::PropertyType::Bool,
        iris::PropertyType::Int,
        iris::PropertyType::Vec2,
        iris::PropertyType::Vec4,
        iris::PropertyType::Texture,
        iris::PropertyType::File,
        iris::PropertyType::List,
    };

    for (const auto type : unsupported) {
        iris::PropertyAnim *anim = makePropertyAnim(type, QStringLiteral("whatever"));
        QVERIFY2(anim == nullptr, qPrintable(QString("type %1 built a track")
                                                 .arg(static_cast<int>(type))));
    }

    // And a value outside the enum entirely (a reflection widened in a future
    // irisgl change, arriving through an int-typed QVariant) is still safe.
    iris::PropertyAnim *bogus =
        makePropertyAnim(static_cast<iris::PropertyType>(99), QStringLiteral("bogus"));
    QVERIFY(bogus == nullptr);
}

void PropertyAnimTest::predicateAgreesWithFactory()
{
    // buildPropertiesMenu() filters on isAnimatablePropertyType(); addPropertyKey()
    // trusts makePropertyAnim(). If those two ever disagree the menu offers a
    // property that cannot be keyed - the drift this pairing exists to prevent.
    for (int i = static_cast<int>(iris::PropertyType::None);
         i <= static_cast<int>(iris::PropertyType::List); ++i) {
        const auto type = static_cast<iris::PropertyType>(i);
        iris::PropertyAnim *anim = makePropertyAnim(type, QStringLiteral("p"));
        QCOMPARE(isAnimatablePropertyType(type), anim != nullptr);
        delete anim;
    }
}

// No QApplication: the factory is pure document code, so the suite needs
// neither a display nor a platform plugin.
QTEST_APPLESS_MAIN(PropertyAnimTest)
#include "test_property_anim.moc"
