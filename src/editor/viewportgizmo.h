#ifndef VIEWPORTGIZMO_H
#define VIEWPORTGIZMO_H

#include "translationgizmo.h"
#include "rotationgizmo.h"

class ViewportGizmo
{
    QSharedPointer<TranslationGizmo> translationGizmo;
    QSharedPointer<RotationGizmo> rotationGizmo;

    ViewportGizmo() {
        translationGizmo = QSharedPointer<TranslationGizmo>();
        rotationGizmo = QSharedPointer<RotationGizmo>();
    }

    void switchGizmo() {

    }

    void discardGizmo() {

    }

    void getActiveGizmo() {

    }
};

#endif // VIEWPORTGIZMO_H
