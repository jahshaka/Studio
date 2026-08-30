#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPainter>
#include "../properties.h"

class FloatPropertyWidget : public QWidget
{
public:
    FloatPropertyWidget():
        QWidget(nullptr)
    {
        buildUi();
    }

    void buildUi()
    {
        auto layout = new QVBoxLayout();
        auto line = new QHBoxLayout();
        line->addWidget(new QLabel("Display Name"));
        displayName = new QLineEdit("Float Property");
        line->addWidget(displayName);
        layout->addLayout(line);

        line = new QHBoxLayout();
        line->addWidget(new QLabel("Value"));
        spinBox = new QDoubleSpinBox();
        line->addWidget(spinBox);
        layout->addLayout(line);

        connect(spinBox, SIGNAL(valueChanged(double)), this, SLOT(valueChanged(double)));
        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [=](double d){
            valueChanged(d);
        });

        this->setLayout(layout);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    }

    void setProperty(FloatProperty* prop)
    {
        this->prop = prop;
        displayName->setText(prop->displayName);
        spinBox->setValue(prop->getValue().toFloat());
    }

    QDoubleSpinBox* spinBox;
    FloatProperty* prop;
    QLineEdit* displayName;

public slots:
	void valueChanged(double newVal)
	{
		prop->setValue((float)newVal);
	}
protected:
	void paintEvent(QPaintEvent *event) override
	{
		QWidget::paintEvent(event);
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(QPen(QColor(200, 200, 200, 70), 2));
		painter.drawRect(0, 0, width(), height());
	}




};
