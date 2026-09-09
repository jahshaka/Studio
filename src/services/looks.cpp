/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/looks.h"

namespace looks
{

namespace
{

QVector<LookUi> buildTable()
{
    QVector<LookUi> out;
    {
        LookUi l;
        l.id = QStringLiteral("desaturate");
        l.label = QStringLiteral("Desaturate");
        l.doc = QStringLiteral(
            "Pulls the colour out of the picture towards its luminance. At 1 the "
            "frame is black and white (Rec.601 weights, the same ones broadcast "
            "uses); anything between is a partial bleach.");
        l.params.append({ QStringLiteral("amount"), QStringLiteral("Amount"), 0.01, 2,
                          QStringLiteral("How far towards grey, 0..1. At 0 the look is "
                                         "an exact identity — the frame is bit for bit "
                                         "the frame with no look at all.") });
        out.append(l);
    }
    {
        LookUi l;
        l.id = QStringLiteral("glassWarp");
        l.label = QStringLiteral("Glass Warp");
        l.doc = QStringLiteral(
            "Refraction through rippled glass: the picture is resampled through a "
            "displaced coordinate, so nothing is added or lost — it moves. Heat "
            "haze, a shower door, a dream transition.");
        l.params.append({ QStringLiteral("amount"), QStringLiteral("Amount"), 0.01, 2,
                          QStringLiteral("How far the ripple displaces the image, 0..1. "
                                         "At 0 the look is an exact identity.") });
        l.params.append({ QStringLiteral("scale"), QStringLiteral("Ripple Scale"), 0.05, 2,
                          QStringLiteral("How many ripples fit across the frame. Low values "
                                         "lean the whole image; high ones read as texture.") });
        out.append(l);
    }
    {
        LookUi l;
        l.id = QStringLiteral("radialBlur");
        l.label = QStringLiteral("Radial Blur");
        l.doc = QStringLiteral(
            "A zoom blur streaking out from a point — speed, impact, a lens rack. "
            "The middle of the frame stays sharp and the blur comes on with "
            "distance from the centre, so the subject survives the effect.");
        l.params.append({ QStringLiteral("amount"), QStringLiteral("Amount"), 0.01, 2,
                          QStringLiteral("How far the seven taps spread, 0..1. At 0 the look "
                                         "is an exact identity.") });
        l.params.append({ QStringLiteral("centerX"), QStringLiteral("Center X"), 0.005, 3,
                          QStringLiteral("Where the streaks come from, across the frame "
                                         "(0 left, 1 right). Outside 0..1 is allowed and "
                                         "streaks everything one way.") });
        l.params.append({ QStringLiteral("centerY"), QStringLiteral("Center Y"), 0.005, 3,
                          QStringLiteral("Where the streaks come from, up the frame "
                                         "(0 top, 1 bottom).") });
        l.params.append({ QStringLiteral("falloff"), QStringLiteral("Falloff"), 0.02, 2,
                          QStringLiteral("How fast the blur comes on with distance from the "
                                         "centre. 1 is linear; higher keeps more of the middle "
                                         "sharp.") });
        out.append(l);
    }
    {
        LookUi l;
        l.id = QStringLiteral("oldMovie");
        l.label = QStringLiteral("Old Movie");
        l.doc = QStringLiteral(
            "Projected film: sepia, dirt on the gate, exposure flicker and frame "
            "weave. The only look here that ANIMATES — it moves on its own, at "
            "film's own 24 frames a second, and a still frame of it is not the "
            "whole effect.");
        l.params.append({ QStringLiteral("amount"), QStringLiteral("Amount"), 0.01, 2,
                          QStringLiteral("How much of the aged picture replaces the original, "
                                         "0..1. At 0 the look is an exact identity.") });
        l.params.append({ QStringLiteral("flicker"), QStringLiteral("Flicker"), 0.01, 2,
                          QStringLiteral("Exposure wandering between shots of the gate. 0 is a "
                                         "steady projector.") });
        l.params.append({ QStringLiteral("dirt"), QStringLiteral("Dirt"), 0.01, 2,
                          QStringLiteral("Splotches and specks on the print, redrawn every "
                                         "frame. 0 is a clean print.") });
        l.params.append({ QStringLiteral("jitter"), QStringLiteral("Frame Jitter"), 0.01, 2,
                          QStringLiteral("Gate weave — how far the whole image slides up and "
                                         "down between frames. 0 is a rock-steady gate.") });
        out.append(l);
    }
    {
        LookUi l;
        l.id = QStringLiteral("posterize");
        l.label = QStringLiteral("Posterize");
        l.doc = QStringLiteral(
            "Quantizes the picture to a small number of levels per channel — silk "
            "screen, cel shading, a comic. The quantization happens in a bent "
            "colour space so the bands fall where the eye puts them.");
        l.params.append({ QStringLiteral("amount"), QStringLiteral("Amount"), 0.01, 2,
                          QStringLiteral("How much of the banded picture replaces the "
                                         "original, 0..1. At 0 the look is an exact "
                                         "identity.") });
        l.params.append({ QStringLiteral("levels"), QStringLiteral("Levels"), 0.1, 1,
                          QStringLiteral("How many steps each colour channel is allowed. 2 is "
                                         "a hard duotone; above 32 an 8-bit image cannot show "
                                         "the difference.") });
        l.params.append({ QStringLiteral("gamma"), QStringLiteral("Gamma"), 0.01, 2,
                          QStringLiteral("Where the bands fall. Below 1 they crowd into the "
                                         "shadows, which is what makes posterize look "
                                         "deliberate rather than blocky.") });
        out.append(l);
    }
    {
        LookUi l;
        l.id = QStringLiteral("sharpen");
        l.label = QStringLiteral("Sharpen");
        l.doc = QStringLiteral(
            "An unsharp mask over the finished frame — the standard place for it, "
            "after anti-aliasing rather than before. Adds local contrast at edges; "
            "too much rings.");
        l.params.append({ QStringLiteral("amount"), QStringLiteral("Amount"), 0.01, 2,
                          QStringLiteral("How strongly edges are lifted, 0..1. At 0 the "
                                         "correction is exactly zero and the look is an exact "
                                         "identity.") });
        out.append(l);
    }
    {
        LookUi l;
        l.id = QStringLiteral("filmGrade");
        l.label = QStringLiteral("Film Grade");
        l.doc = QStringLiteral(
            "The four controls a colourist reaches for first — saturation, "
            "contrast, tint and vignette — applied in that order to the finished "
            "frame. Every parameter is neutral at its default, so a fresh Film "
            "Grade changes nothing until you move something.");
        l.params.append({ QStringLiteral("amount"), QStringLiteral("Amount"), 0.01, 2,
                          QStringLiteral("How much of the graded picture replaces the "
                                         "original, 0..1. At 0 the look is an exact "
                                         "identity.") });
        l.params.append({ QStringLiteral("saturation"), QStringLiteral("Saturation"), 0.01, 2,
                          QStringLiteral("1 is untouched, 0 is black and white, above 1 pushes "
                                         "colour.") });
        l.params.append({ QStringLiteral("contrast"), QStringLiteral("Contrast"), 0.01, 2,
                          QStringLiteral("1 is untouched. Pivots about mid grey, so raising it "
                                         "does not change the overall brightness.") });
        l.params.append({ QStringLiteral("vignette"), QStringLiteral("Vignette"), 0.01, 2,
                          QStringLiteral("Darkens the corners. 0 is off; 1 takes the corners "
                                         "to black and leaves the centre untouched.") });
        l.params.append({ QStringLiteral("tintR"), QStringLiteral("Tint R"), 0.01, 2,
                          QStringLiteral("Red multiplier; 1 is neutral.") });
        l.params.append({ QStringLiteral("tintG"), QStringLiteral("Tint G"), 0.01, 2,
                          QStringLiteral("Green multiplier; 1 is neutral.") });
        l.params.append({ QStringLiteral("tintB"), QStringLiteral("Tint B"), 0.01, 2,
                          QStringLiteral("Blue multiplier; 1 is neutral. Below 1 on blue and "
                                         "green is the warm print look.") });
        out.append(l);
    }
    return out;
}

}   // namespace

const QVector<LookUi> &table()
{
    static const QVector<LookUi> t = buildTable();
    return t;
}

const LookUi *lookUi(const QString &id)
{
    for (const LookUi &l : table())
        if (l.id == id) return &l;
    return nullptr;
}

const ParamUi *paramUi(const QString &lookId, const QString &paramId)
{
    const LookUi *l = lookUi(lookId);
    if (!l) return nullptr;
    for (const ParamUi &p : l->params)
        if (p.id == paramId) return &p;
    return nullptr;
}

QString validate()
{
    int count = 0;
    const iris::LookDef *cat = iris::lookCatalogue(count);
    if (count != table().size())
        return QStringLiteral("look count: document %1, presentation %2")
            .arg(count).arg(table().size());
    for (int i = 0; i < count; ++i) {
        const iris::LookDef &def = cat[i];
        const QString id = QString::fromLatin1(def.id);
        const LookUi *ui = lookUi(id);
        if (!ui) return QStringLiteral("look '%1' has no presentation row").arg(id);
        if (ui->label.isEmpty()) return QStringLiteral("look '%1' has no label").arg(id);
        if (ui->params.size() != def.paramCount)
            return QStringLiteral("look '%1': document has %2 parameters, presentation %3")
                .arg(id).arg(def.paramCount).arg(ui->params.size());
        for (int j = 0; j < def.paramCount; ++j) {
            const QString pid = QString::fromLatin1(def.params[j].id);
            if (ui->params[j].id != pid)
                return QStringLiteral("look '%1' parameter %2: document '%3', presentation '%4'")
                    .arg(id).arg(j).arg(pid, ui->params[j].id);
            if (ui->params[j].label.isEmpty())
                return QStringLiteral("look '%1' parameter '%2' has no label").arg(id, pid);
        }
    }
    for (const LookUi &l : table())
        if (!iris::lookDef(l.id))
            return QStringLiteral("presentation row '%1' names no document look").arg(l.id);
    return QString();
}

}   // namespace looks
