#ifndef SCENEIOBASE_H
#define SCENEIOBASE_H

#include <QDir>

class SceneIOBase
{
protected:

    //holds the directory for the file being saved or loaded
    //used for creating relative file paths for assets upon saving scene
    //used for creating absolute file paths for assets upon loading scene
    QDir dir;


    //returns QDir containing filename's parent folder
    static QDir getDirFromFileName(QString filename);

    //gets relative path for filename
    //assumes dir has already been assigned a value from saveScene or loadScene
    //should be called inside a SceneNode's writeData function
    //if path is resource, return original path
    QString getRelativePath(QString filename);

    //gets absolute path for filename
    //assumes dir has already been assigned a value from saveScene or loadScene
    //should be called inside a SceneNode's readData function
    //returns original string if filepath is a resource
    //returns null string if path doesnt exist
    QString getAbsolutePath(QString filename);
};

#endif // SCENEIOBASE_H
