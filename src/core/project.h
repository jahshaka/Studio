/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECT_H
#define PROJECT_H
#include <QString>

class Project
{
public:
    QString folderPath;
    QString projectName;
    QString guid;

    bool _saved;
public:
    Project();
    void setProjectPath(const QString&);
    void setProjectGuid(const QString&);
    bool isSaved();

    QString getProjectName();
    QString getProjectFolder();
    QString getProjectGuid();

    static Project* createNew();
};

struct ProjectTileData {
    QString     name;
    QByteArray  thumbnail;
    QString     guid;
};

struct AssetTileData {
	QString     name;
	QString     full_filename;
	QByteArray  thumbnail;
	QString     guid;
    QString     collection_name;
	int			type;
	int			collection;
	QByteArray  properties;
	QString		license;
	QString		author;
	QByteArray  tags;
};

struct AssetData {
	QByteArray thumbnail;
	short type;
	QString guid;
	QString name;
	QString extension;
};

struct CollectionData {
    QString     name;
    int         id;
};

enum class ModelTypes {
	Shader,
	Material,
	Texture,
	Video,
	Cubemap,
	Object,
	SoundEffect,
	Music
};

#define	MODEL_GUID_ROLE	0x0113
#define	MODEL_EXT_ROLE	0x0133
#define	MODEL_TYPE_ROLE	0x0123

#endif // PROJECT_H
