/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

// Node-graph visual style constants, adapted from the NodeGraphQt design
// (https://github.com/jchanvfx/NodeGraphQt, MIT, Copyright (c) 2017 Johnny
// Chan). Values follow NodeGraphQt's constants.py / qgraphics/* tables;
// the category-colour language (header strips, muted border tints) is ours.

#pragma once

#include <QColor>

namespace NodeStyle
{
// ---------------------------------------------------------------- node card
namespace Node
{
    constexpr qreal  radius = 4.0;              // rounded corner radius
    inline const QColor fill{13, 18, 23};       // near-black card
    inline const QColor border{46, 57, 66};     // thin resting border
    constexpr qreal  borderWidth = 0.8;

    inline const QColor selectedBorder{254, 207, 42};   // yellow
    constexpr qreal  selectedBorderWidth = 1.2;
    inline const QColor selectedWash{255, 255, 255, 30};// translucent overlay

    // upstream-chain highlight (our feature, thinned to NGQt pen weights)
    inline const QColor chainRootBorder{55, 155, 255};  // selected node
    inline const QColor chainLinkBorder{250, 250, 50};  // its upstream nodes
    constexpr qreal  chainBorderWidth = 1.2;

    // title area: dark overlay + slim category-colour strip
    inline const QColor titleOverlay{0, 0, 0, 80};
    constexpr qreal  titleOverlayRadius = 3.0;
    inline const QColor titleText{255, 255, 255, 180};
    constexpr int    titleStripHeight = 3;      // category colour strip
    constexpr int    iconSize = 18;             // header icon pixmap

    // master (output) node: brighter, wider strip so it reads as special
    constexpr int    masterStripHeight = 6;
    inline const QColor masterTitleOverlay{255, 255, 255, 24};

    // muted category tint for the resting border (mixed toward Node::border)
    inline QColor mutedBorder(const QColor& category)
    {
        if (!category.isValid() || category.alpha() == 0)
            return border;
        return QColor((category.red()   + 2 * border.red())   / 3,
                      (category.green() + 2 * border.green()) / 3,
                      (category.blue()  + 2 * border.blue())  / 3);
    }
}

// ------------------------------------------------------------------- ports
namespace Port
{
    // three paint states (NodeGraphQt port.py); the ring takes the port's
    // TYPE colour when idle/connected so the type language stays visible.
    inline const QColor idleFill{49, 115, 100};
    inline const QColor idleBorder{29, 202, 151};
    inline const QColor hoverFill{17, 43, 82};
    inline const QColor hoverBorder{136, 255, 35};
    inline const QColor connectedFill{14, 45, 59};
    inline const QColor connectedBorder{107, 166, 193};
    constexpr qreal  borderWidth = 1.8;

    // live-drag feedback on candidate target sockets
    inline const QColor validTargetBorder{136, 255, 35};
    inline const QColor invalidTargetBorder{150, 60, 255};

    // per-type colour palette (was: float/vec2/3/4 all one teal)
    inline const QColor typeFloat{99, 170, 170};    // soft teal
    inline const QColor typeVec2{160, 130, 209};    // violet
    inline const QColor typeVec3{208, 168, 92};     // gold
    inline const QColor typeVec4{199, 110, 154};    // rose
    inline const QColor typeTexture{60, 155, 60};   // green (kept)
}

// ------------------------------------------------------------------- pipes
namespace Pipe
{
    constexpr qreal  width = 1.5;
    inline const QColor color{175, 95, 30};
    inline const QColor hover{70, 255, 220};
    constexpr qreal  hoverWidth = 3.0;
    inline const QColor selected{232, 184, 13};
    constexpr qreal  selectedWidth = 2.0;
    inline const QColor liveDrag{70, 255, 220};     // dashed while dragging
    inline const QColor liveInvalid{150, 60, 255};  // over incompatible target
    constexpr qreal  liveWidth = 1.5;
    constexpr qreal  pickWidth = 12.0;              // fat shape() for clicking
    // curved-layout tangent clamp (== default node width)
    constexpr qreal  maxTangent = 170.0;
}

// ------------------------------------------------------------------ canvas
namespace Canvas
{
    inline const QColor background{35, 35, 35};
    inline const QColor grid{45, 45, 45};
    constexpr int    gridSize = 50;
    constexpr int    gridZoomFactor = 8;            // coarse grid = 8x fine
    inline const QColor gridCoarse{30, 30, 30};
    constexpr qreal  zoomMin = 0.15;                // clamp zoom-out
    constexpr qreal  zoomMax = 2.0;                 // clamp zoom-in
    constexpr qreal  zoomStep = 1.2;
    constexpr qreal  fitMargin = 60.0;              // scene units around F-fit
}

// ------------------------------------------------------- shared stylesheets
// One QMenu sheet (was copy-pasted in graphnodescene.cpp and listwidget.cpp)
inline const char* const menuStyleSheet =
    "QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
    "QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
    "QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
    "QMenu::item:disabled { color: #555; }";
}
