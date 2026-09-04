/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.property_anim — the keyframe edit service (src/services/animationedits.h).
//
// Half of this suite is the original subject: the PropertyAnim factory never
// hands back an indeterminate pointer (ENGINEERING_DEBT_SPEC item 8). The other
// half is the write path the Timeline panel and the anim.* verbs now SHARE
// (DEEP_AUDIT_2026_09 List B #7) — one keyframe writer, so the panel's insert
// button and a script cannot key a property differently.
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

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"

#include "services/animationedits.h"

using animedits::isAnimatablePropertyType;
using animedits::makePropertyAnim;

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

    //! A second key at the same time overwrites — two keys at one time is an
    //! interpolation with a zero denominator, not "two keys".
    void keyAtSameTimeOverwrites();

    //! Removing keys puts the derived animation length back down (KeyFrame's
    //! own length field is only maintained by addKey).
    void removingKeysShortensTheAnimation();

    //! Removing a track that is not there reports false and — the QMap
    //! operator[] trap — does NOT create one.
    void removingAbsentTrackIsNoOp();
};

namespace {

//! A Vec3 "position" property carrying `value`.
animedits::PropertyInfo positionProp(const QVector3D &value)
{
    animedits::PropertyInfo prop;
    prop.name = QStringLiteral("position");
    prop.displayName = QStringLiteral("Position");
    prop.type = iris::PropertyType::Vec3;
    prop.value = value;
    prop.index = 0;
    return prop;
}

}   // namespace

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

void PropertyAnimTest::keyAtSameTimeOverwrites()
{
    auto anim = iris::Animation::create(QStringLiteral("Take 1"));

    bool created = false;
    QString error;
    QVERIFY2(animedits::setKeyframe(anim, positionProp(QVector3D(0, 0, 0)), 0.0,
                                    QVariant(), &created, &error),
             qPrintable(error));
    QVERIFY(created);
    QVERIFY(animedits::setKeyframe(anim, positionProp(QVector3D(10, 0, 0)), 2.0,
                                   QVariant(), &created, &error));
    QVERIFY2(!created, "the second write reused the track");

    auto *track = anim->getPropertyAnim(QStringLiteral("position"));
    QVERIFY(track != nullptr);
    const auto frames = track->getKeyFrames();
    QCOMPARE(frames.count(), 3);
    QCOMPARE(frames[0].keyFrame->keys.count(), 2);
    QCOMPARE(anim->getLength(), 2.0f);

    // Same time again: overwrite, not a third key.
    QVERIFY(animedits::setKeyframe(anim, positionProp(QVector3D(20, 0, 0)), 2.0));
    QCOMPARE(frames[0].keyFrame->keys.count(), 2);
    QCOMPARE(frames[0].keyFrame->keys.last()->value, 20.0f);

    // And the value can come from the caller rather than the property.
    QVERIFY(animedits::setKeyframe(anim, positionProp(QVector3D(0, 0, 0)), 2.0,
                                   QVariant::fromValue(QVector3D(30, 0, 0))));
    QCOMPARE(frames[0].keyFrame->keys.last()->value, 30.0f);

    QCOMPARE(animedits::sampleTrack(anim, QStringLiteral("position"), 1.0)
                 .value<QVector3D>().x(), 15.0f);
}

void PropertyAnimTest::removingKeysShortensTheAnimation()
{
    auto anim = iris::Animation::create(QStringLiteral("Take 2"));
    QVERIFY(animedits::setKeyframe(anim, positionProp(QVector3D(0, 0, 0)), 0.0));
    QVERIFY(animedits::setKeyframe(anim, positionProp(QVector3D(1, 0, 0)), 1.0));
    QVERIFY(animedits::setKeyframe(anim, positionProp(QVector3D(2, 0, 0)), 4.0));
    QCOMPARE(anim->getLength(), 4.0f);

    // Three channels, so three keys go.
    QCOMPARE(animedits::removeKeyframe(anim, QStringLiteral("position"), 4.0), 3);
    QCOMPARE(anim->getLength(), 1.0f);
    QCOMPARE(animedits::removeKeyframe(anim, QStringLiteral("position"), 4.0), 0);

    QVERIFY(animedits::removeTrack(anim, QStringLiteral("position")));
    QVERIFY(!anim->hasPropertyAnim(QStringLiteral("position")));
    QCOMPARE(anim->getLength(), 0.0f);
}

void PropertyAnimTest::removingAbsentTrackIsNoOp()
{
    auto anim = iris::Animation::create(QStringLiteral("Take 3"));

    QVERIFY(!animedits::removeTrack(anim, QStringLiteral("scale")));
    QCOMPARE(animedits::removeKeyframe(anim, QStringLiteral("scale"), 0.0), 0);
    QVERIFY(!animedits::sampleTrack(anim, QStringLiteral("scale"), 0.0).isValid());

    // The trap this guards: Animation's getters used to read the map with
    // QMap::operator[], which INSERTS a null entry for a missing key — so
    // merely asking about "scale" left a track behind.
    QVERIFY2(anim->properties.isEmpty(), "a lookup created a track");
    QVERIFY(!anim->hasPropertyAnim(QStringLiteral("scale")));
    QVERIFY(anim->getPropertyAnim(QStringLiteral("scale")) == nullptr);
    QVERIFY(anim->properties.isEmpty());
}

// No QApplication: the service is pure document code, so the suite needs
// neither a display nor a platform plugin.
QTEST_APPLESS_MAIN(PropertyAnimTest)
#include "test_property_anim.moc"
