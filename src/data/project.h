/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECT_H
#define PROJECT_H

#include <QString>
#include <QDateTime>

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
    void setProjectPath(const QString&, const QString &);
    void setProjectGuid(const QString&);
    bool isSaved();

    QString getProjectName();
    QString getProjectFolder();
    QString getProjectGuid();

    static Project *createNew();

    static const QStringList ModelTypesAsString;
};

struct ProjectTileData {
    QString     name;
    QByteArray  thumbnail;
    QString     guid;
    int         desktop = 1;        // which desktop (1..4) the project lives on
    bool        hasPosition = false;// freeform position ever assigned?
    float       posX = 0.f;         // normalized 0..1 on the desktop canvas
    float       posY = 0.f;
    // Slider mode (DESKTOP_SLIDER_SPEC.md): per-tile filmstrip assignment,
    // kept beside the freeform position so mode switches are lossless.
    bool        hasSliderPos = false;
    int         sliderRow = 0;      // 0-based filmstrip row
    int         sliderIndex = 0;    // 0-based order within the row
};

enum AssetViewFilter : int
{
	Editor = 1,
	AssetsView,
	Effects,
	DontShow // Use for dependencies and hidden types later on
};

struct DatabaseMetadataRecord
{
	QDateTime dateCreated;
	QString hash;
	QString version;
	QByteArray data;

	// specific version
	int major;
	int minor;
	int patch;
};

// The int fields default: not every fetch query selects every column, and an
// unselected column left the field UNINITIALIZED — assets.list's project
// scope returned garbage ints in `drawer` (record.collection) for records
// fetched through fetchChildAssets. 0 = Uncategorized for collection.
struct AssetRecord
{
    QString     guid;
	int			type = 0;
	QString     name;
	int			collection = 0;
	int			timesUsed = 0;
    QString     projectGuid;
    QDateTime   dateCreated;
    QDateTime   lastUpdated;
	QString		author;
	QString		license;
	QString		hash;
	QString		version;
	QString		parent;
	QByteArray  thumbnail;
	QByteArray  asset;
	QByteArray  tags;
	QByteArray  properties;
	int			view_filter = 0;
};

struct DependencyRecord
{
    int         dependerType;
    int         dependeeType;
    QString     projectGuid;
    QString     depender;
    QString     dependee;
    QString     id;
};

struct FolderRecord
{
	QString	    guid;
	QString	    name;
	QString	    parent;
    QString     version;
    QString     projectGuid;
    QDateTime   dateCreated;
    QDateTime   lastUpdated;
	int	        count;
    bool        visible;
};

struct CollectionRecord
{
    QString     name;
    QDateTime   dateCreated;
    int         id;
    int         parent = -1;    // parent drawer id; -1 = top level (ASSET_DRAWERS_SPEC §2)
};

enum class ModelTypes
{
    Undefined,		// Used
    Material,		// Supported
    Texture,		// Supported
    Video,
    Sky,			// Supported
    Object,			// Supported
    Mesh,			// Supported
    SoundEffect,
    Music,			// Supported
    Shader,			// Supported
    Variant,
    File,			// Supported
    ParticleSystem,	// Supported
    // IES photometric profiles (.ies). A first-class library type rather than a
    // whitelisted plain file: it needs its own validator (a bad .ies would
    // otherwise become a renderer exception at first draw), its own metadata
    // block (the photometric scale the mirror divides intensity by) and its own
    // thumbnail (a polar lobe plot — the only way to tell two profiles apart).
    // APPENDED, never inserted: the value is persisted in every assets row.
    LightProfile	// Supported
};

#define	MODEL_GUID_ROLE		0x0113
#define MODEL_PARENT_ROLE	0x0128
#define	MODEL_EXT_ROLE		0x0133
#define	MODEL_TYPE_ROLE		0x0123
#define	MODEL_MESH_ROLE		0x0173
#define SKY_TYPE_ROLE		0x0179

#define MODEL_ITEM_TYPE		0x0981
#define MODEL_FOLDER		0x0871
#define MODEL_ASSET			0x0421

#define MODEL_GRAPH			0x0321

#endif // PROJECT_H
