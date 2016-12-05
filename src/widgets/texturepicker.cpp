#include "texturepicker.h"
#include "ui_texturepicker.h"
#include "qfiledialog.h"
#include <Qt>
#include "../core/thumbnailmanager.h"

TexturePicker::TexturePicker(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TexturePicker)
{
    ui->setupUi(this);
    connect(ui->load,SIGNAL(pressed()),SLOT(changeDiffuseMap()));

    this->ui->DiffuseMap->installEventFilter(this);
}

TexturePicker::~TexturePicker()
{
    delete ui;
}

void TexturePicker::changeDiffuseMap()
{
    auto file = loadTexture();
    if(file.isEmpty() || file.isNull())
        return;

    this->setLabelImage(ui->DiffuseMap,file);
}

QString TexturePicker::loadTexture()
{
    QString dir = QApplication::applicationDirPath();
    return QFileDialog::getOpenFileName(this,"Open Texture File",dir,"Image Files (*.png *.jpg *.bmp)");
}

void TexturePicker::setLabelImage(QLabel* label,QString file)
{
    if(file.isNull() || file.isEmpty())
    {
        ui->DiffuseMap->clear();
    }
    else
    {
        auto thumb = ThumbnailManager::createThumbnail(file,ui->DiffuseMap->width(),ui->DiffuseMap->height());
        QPixmap pixmap = QPixmap::fromImage(*thumb->thumb);
        ui->DiffuseMap->setPixmap(pixmap);
        //d_height = image.height();
        //d_width = image.width();

        QFileInfo fileInfo(file);
        filename = fileInfo.fileName();
        ui->imagename->setText(filename);
        ui->imagename->setMaximumWidth(200);
        ui->imagename->setWordWrap(true);


        QString dimension_H = QString::number(d_height);
        QString dimension_W = QString::number(d_width);
        ui->dimensions->setText( dimension_H + " X " +dimension_W );
        emit valueChanged(file);

    }
}

bool TexturePicker::eventFilter(QObject *object, QEvent *ev)
{
    if(object==ui->DiffuseMap && ev->type()==QEvent::MouseButtonRelease)
    {
        changeDiffuseMap();
    }

    return false;
}

void TexturePicker::on_pushButton_clicked()
{
    ui->imagename->clear();
    ui->dimensions->clear();
    ui->DiffuseMap->clear();

    emit valueChanged(QString::null);
}

void TexturePicker::setTexture(QString path){

    if(path.isNull() || path.isEmpty())
    {
        ui->DiffuseMap->clear();
    }
    else
    {
        setLabelImage(ui->DiffuseMap,path);
    }
}
