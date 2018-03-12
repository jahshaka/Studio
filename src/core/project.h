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
	QString projectFolderGuid;
    QString guid;

    bool _saved;
public:
    Project();
    void setProjectPath(const QString&);
    void setProjectGuid(const QString&);
	void setProjectFolderGuid(const QString &guid);
    bool isSaved();

    QString getProjectName();
    QString getProjectFolder();
    QString getProjectGuid();
	QString getProjectFolderGuid();

    static Project* createNew();
};

struct ProjectTileData {
    QString     name;
    QByteArray  thumbnail;
    QString     guid;
};

struct AssetTileData {
	QString		parent;
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
	bool		used;
};

struct FolderData
{
	QString	guid;
	QString	name;
	QString	parent;
	short	count;
};

struct AssetData {
	QByteArray	thumbnail;
	short		type;
	QString		guid;
	QString		name;
	QString		extension;
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
	Music,
	Undefined
};

#define	MODEL_GUID_ROLE		0x0113
#define MODEL_PARENT_ROLE	0x0128
#define	MODEL_EXT_ROLE		0x0133
#define	MODEL_TYPE_ROLE		0x0123
#define	MODEL_MESH_ROLE		0x0173

#define MODEL_ITEM_TYPE		0x0981
#define MODEL_FOLDER		0x0871
#define MODEL_ASSET			0x0421

#endif // PROJECT_H
