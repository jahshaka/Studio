#include "transformeditor.h"
#include "ui_transformeditor.h"

TransformEditor::TransformEditor(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TransformEditor)
{
    ui->setupUi(this);
}

TransformEditor::~TransformEditor()
{
    delete ui;
}
