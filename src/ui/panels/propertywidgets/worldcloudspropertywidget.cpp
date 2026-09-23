/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldcloudspropertywidget.h"

#include <QFileInfo>
#include <QSqlDatabase>

#include "commands/scenepropertycommand.h"
#include "data/project.h"
#include "irisgl/document/scenegraph/scene.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/panels/propertyrows.h"
#include "ui/panels/propertywidgets/rowundo.h"

namespace {
bool drawsOver(const iris::Scene &s)
{
    return s.skyType != iris::SkyType::EQUIRECTANGULAR && s.skyType != iris::SkyType::CUBEMAP;
}
}   // namespace

WorldCloudsPropertyWidget::WorldCloudsPropertyWidget()
    : rows([this]() { return scene; }, [this]() { return services; },
           [this]() { refreshRows(); }, [this]() { return !loading; })
{
    enabled = this->addCheckBox("Clouds", false);
    enabled->setToolTip(QStringLiteral(
        "A sheet of cloud at a fixed altitude, drawn over the Colour, Gradient and Realistic skies. "
        "It is part of the sky: the Sky Light's ambient and every reflection see it, and it shades "
        "the sun's light on the ground. It runs at every World Mode tier and in VR. A full overcast "
        "changes the scene's light and therefore its exposure, which is correct: the key moves."));
    PropertyRows::describe(enabled, { QStringLiteral("clouds"), QStringLiteral("weather"),
                                      QStringLiteral("overcast"), QStringLiteral("sky") });

    coverage = this->addFloatValueSlider("Coverage", 0.f, 1.f, 0.5f);
    coverage->setDecimals(2);
    coverage->setToolTip(QStringLiteral(
        "How much of the sky is cloud: 0 clear, 1 an overcast deck. More cloud dims the sun on the "
        "ground and changes the sky's light — the scene's exposure follows it (correct: an overcast "
        "day is a different key)."));
    density = this->addFloatValueSlider("Density", 0.f, 4.f, 1.f);
    density->setDecimals(2);
    density->setToolTip(QStringLiteral(
        "How opaque a covered patch is — how dark its underside, and how dark its shadow."));
    speed = this->addFloatValueSlider("Wind Speed", 0.f, 100.f, 10.f);
    speed->setDecimals(1);
    speed->setToolTip(QStringLiteral(
        "Metres per second of scene time. A paused scene holds its clouds still."));
    direction = this->addFloatValueSlider("Wind Direction", 0.f, 360.f, 0.f);
    direction->setDecimals(0);
    direction->setToolTip(QStringLiteral(
        "The heading the wind blows towards, in degrees from +X turning towards -Z."));
    altitude = this->addFloatValueSlider("Altitude", 500.f, 8000.f, 2000.f);
    altitude->setDecimals(0);
    altitude->setToolTip(QStringLiteral(
        "The sheet's height in metres over a curved earth: a low layer fills the sky to the horizon, "
        "a high one stays overhead, and a low sun throws the ground shadow further sideways."));
    shadow = this->addFloatValueSlider("Ground Shadow", 0.f, 1.f, 1.f);
    shadow->setDecimals(2);
    shadow->setToolTip(QStringLiteral(
        "How strongly the clouds shade the sun's light on the ground (1 = what the sheet really "
        "lets through). Surfaces that receive no shadows are not shaded."));
    weather = this->addTexturePicker("Weather Map");
    weather->setToolTip(QStringLiteral(
        "Optional. An image whose red channel shapes the cloud cover over one 16 km tile of the "
        "sheet: white lets clouds form, black keeps the sky clear."));
    note = this->addLabel("Note", QString());

    rowundo::bind(enabled, rows(QStringLiteral("clouds"), tr("Clouds"), [this](const QVariant &v) {
        return withField([&v](iris::CloudLayer &c) { c.enabled = v.toBool(); });
    }));
    const auto bindDial = [this](HFloatSliderWidget *row, const QString &text,
                                 float iris::CloudLayer::*field) {
        rowundo::bind(row, rows(QStringLiteral("clouds"), text, [this, field](const QVariant &v) {
            return withField([&v, field](iris::CloudLayer &c) { c.*field = v.toFloat(); });
        }));
    };
    bindDial(coverage, tr("Cloud Coverage"), &iris::CloudLayer::coverage);
    bindDial(density, tr("Cloud Density"), &iris::CloudLayer::density);
    bindDial(speed, tr("Wind Speed"), &iris::CloudLayer::speed);
    bindDial(direction, tr("Wind Direction"), &iris::CloudLayer::direction);
    bindDial(altitude, tr("Cloud Altitude"), &iris::CloudLayer::altitude);
    bindDial(shadow, tr("Cloud Shadow"), &iris::CloudLayer::shadow);
    connect(weather, &TexturePickerWidget::valuesChanged, this,
            [this](QString value, QString guid) { onWeatherPicked(value, guid); });
}

QVariant WorldCloudsPropertyWidget::withField(const std::function<void(iris::CloudLayer &)> &edit) const
{
    if (!scene) return QVariant();
    iris::CloudLayer c = scene->clouds;
    edit(c);
    c = iris::CloudLayer::clamped(c);
    QVariantMap m = c.toJson().toVariantMap();
    if (scene->cloudWeatherMap && !c.weatherMapGuid.isEmpty())
        m.insert(QStringLiteral("weatherPath"), scene->cloudWeatherMap->source);
    return m;
}

// THE WEATHER MAP. The picker carries the asset guid of the row the user chose
// or dropped; its bytes are the project's pin (the CAS), like the sky's image.
// One undo step through the same "clouds" key, and the path of the pixels rides
// with it so an undo restores the map it replaced.
void WorldCloudsPropertyWidget::onWeatherPicked(const QString &value, const QString &guid)
{
    if (loading || !scene) return;
    QString path;
    if (!guid.isEmpty() && project)
        path = AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                       project->getProjectGuid(), guid);
    if (!guid.isEmpty() && (path.isEmpty() || !QFileInfo::exists(path))) return;
    Q_UNUSED(value)
    iris::CloudLayer c = scene->clouds;
    c.weatherMapGuid = path.isEmpty() ? QString() : guid;
    QVariantMap m = iris::CloudLayer::clamped(c).toJson().toVariantMap();
    if (!path.isEmpty()) m.insert(QStringLiteral("weatherPath"), path);
    const QVariant before = sceneprops::get(scene, QStringLiteral("clouds"));
    sceneprops::set(scene, QStringLiteral("clouds"), m);
    panelundo::pushSceneEdit(services, scene, QStringLiteral("clouds"), tr("Weather Map"), before,
                             QVariant(m), [this]() { refreshRows(); });
}

void WorldCloudsPropertyWidget::setScene(QSharedPointer<iris::Scene> s)
{
    scene = s;
    refreshRows();
}

void WorldCloudsPropertyWidget::refreshRows()
{
    if (!scene) return;
    loading = true;
    const iris::CloudLayer &c = scene->clouds;
    enabled->setValue(c.enabled);
    coverage->setValue(c.coverage);
    density->setValue(c.density);
    speed->setValue(c.speed);
    direction->setValue(c.direction);
    altitude->setValue(c.altitude);
    shadow->setValue(c.shadow);
    weather->setTexture(scene->cloudWeatherMap && !c.weatherMapGuid.isEmpty()
                            ? scene->cloudWeatherMap->source : QString());
    // OVER AN IMAGE SKY THE ROWS ARE DISABLED AND SAY WHY: a photograph carries
    // its own clouds, and the layer is never drawn over one.
    const bool over = drawsOver(*scene);
    for (QWidget *w : { static_cast<QWidget *>(enabled), static_cast<QWidget *>(coverage),
                        static_cast<QWidget *>(density), static_cast<QWidget *>(speed),
                        static_cast<QWidget *>(direction), static_cast<QWidget *>(altitude),
                        static_cast<QWidget *>(shadow), static_cast<QWidget *>(weather) })
        w->setEnabled(over);
    note->setText(over
        ? QStringLiteral("Runs at every tier and in VR.")
        : QStringLiteral("Not drawn over an image sky: a photograph carries its own clouds."));
    loading = false;
}
