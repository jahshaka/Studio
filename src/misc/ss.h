//
//  SS.hpp
//  color_picker
//
//  Created by will on 2/3/19.
//

#include <QWidget>

class SS : public QObject
{
public:
    static QString QPushButtonInvisible();
    static QString QPushButtonGreyscale();
    static QString QPushButtonRounded(int size);
    static QString QSpinBox();
    static QString QSlider();
    static QString QLineEdit();
    static QString QWidgetDark();
    static QString QLabelWhite();
    static QString QComboBox();
    static QString QPushButtonGrouped();
};


