/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LOOKS_SERVICE_H
#define LOOKS_SERVICE_H

// THE LOOKS CATALOGUE'S PRESENTATION HALF (SPECS/POST_LOOKS_SPEC.md §4.1).
//
// The CONTRACT — which looks exist, their stored ids, their parameters, the
// defaults and the ranges — lives on the document, in
// irisgl/document/scenegraph/looks.h, because SceneMirror and the scene reader
// need it and neither can see src/. What lives HERE is everything only a human
// needs: labels, tooltips, scrub sensitivity, decimal places.
//
// NOTHING IS RESTATED. A range appears in exactly one file (the document's),
// and this table is keyed on the document's ids — so the panel, the verbs and
// the generated docs cannot disagree with the renderer about what a number
// means, which is the failure services/worldmodes.h exists to prevent for the
// quality rows. The only thing that CAN drift is a missing label, and
// looks::validate() (asserted by tests/services) catches exactly that.
//
// The consumers: the World > Post Process panel's Looks sub-section, the camera
// panel's override rows, and world.lookCatalogue() — which is what a script and
// docs/SCRIPTING.md read.

#include <QString>
#include <QVector>

#include "irisgl/document/scenegraph/looks.h"

namespace looks
{

/// The human half of one parameter. `step` and `decimals` are the same two
/// numbers worldmodes::ParamRow carries, for the same widget
/// (`addDragFloat`) — a look parameter scrubs exactly like AO Radius does.
struct ParamUi {
    QString id;          ///< matches iris::LookParamDef::id
    QString label;
    double  perPixelStep = 0.01;
    int     decimals = 2;
    QString doc;         ///< the row tooltip AND the verb documentation
};

/// The human half of one look.
struct LookUi {
    QString          id;      ///< matches iris::LookDef::id
    QString          label;
    QString          doc;
    /// False renders the row disabled — declared but not implemented by this
    /// build's renderer (the worldmodes::Row `available` precedent). Every
    /// shipped look is true; the flag exists so a catalogue entry can land
    /// ahead of its shader without lying to the panel.
    bool             available = true;
    QVector<ParamUi> params;
};

/// The presentation table, in panel order (the same order as the document's
/// catalogue).
const QVector<LookUi> &table();
/// The entry for this look id, or null.
const LookUi *lookUi(const QString &id);
/// The entry for one parameter of one look, or null.
const ParamUi *paramUi(const QString &lookId, const QString &paramId);

/// Every document look has a presentation row, every row names a document look,
/// and their parameters agree one for one. Empty means agreement; otherwise the
/// message names the first disagreement. The unit test asserts it, and it is
/// the only thing that can rot when a look is added.
QString validate();

}   // namespace looks

#endif   // LOOKS_SERVICE_H
