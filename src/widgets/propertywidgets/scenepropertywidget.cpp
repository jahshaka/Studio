#include "scenepropertywidget.h" // Or the correct header path

ScenePropertyWidget::ScenePropertyWidget(QWidget* parent)
    : AccordianBladeWidget(parent) // Initialize base class
{
    // Constructor code here (e.g., initialization, UI setup)
}

void ScenePropertyWidget::setScene(QSharedPointer<iris::Scene> sceneNode)
{
    this->sceneNode = sceneNode;
    // Implementation for setScene
}

// Implement other methods here