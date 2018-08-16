/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetview.h"
#include "core/settingsmanager.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/preferences/worldsettings.h"

#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/zip/zip.h"

#include <QStackedLayout>
#include <QDirIterator>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QtAlgorithms>
#include <QFile>
#include <QBuffer>
#include <QTreeWidget>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDesktopServices>
#include <QTemporaryDir>

#include "../globals.h"
#include "../constants.h"
#include "../core/settingsmanager.h"
#include "../core/database/database.h"
#include "../uimanager.h"
#include "assetviewgrid.h"
#include "assetgriditem.h"
#include "assetviewer.h"
#include "core/assethelper.h"
#include "io/assetmanager.h"

#include "../core/guidmanager.h"

#include "dialogs/toast.h"

void AssetView::focusInEvent(QFocusEvent *event)
{
	Q_UNUSED(event);
	//emit fetch();
}

bool AssetView::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == assetDropPad) {
		switch (event->type()) {
			case QEvent::Drop: {
				auto evt = static_cast<QDropEvent*>(event);
				QList<QUrl> droppedUrls = evt->mimeData()->urls();
				QStringList list;

				for (auto url : droppedUrls) {
					auto fileInfo = QFileInfo(url.toLocalFile());
					list << fileInfo.absoluteFilePath();
				}

				if (QFileInfo(list.front()).suffix() == "jaf") {
					importJahModel(list.front());
				}
				//else {
				//	importModel(list.front());
				//}

				break;
			}

			case QEvent::DragEnter: {
				auto evt = static_cast<QDragEnterEvent*>(event);
				if (evt->mimeData()->hasUrls()) {
					evt->acceptProposedAction();
				}

				break;
			}

			default: break;
		}
	}

	return QObject::eventFilter(watched, event);
}

void AssetView::copyTextures(const QString &folderGuid)
{
    const QString relativePath = "Textures";
    const aiScene *scene = viewer->ssource->importer.GetScene();

    QStringList texturesToCopy;

    for (int i = 0; i < scene->mNumMeshes; i++) {
        auto mesh = scene->mMeshes[i];
        auto material = scene->mMaterials[mesh->mMaterialIndex];

        aiString textureName;

        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            material->GetTexture(aiTextureType_DIFFUSE, 0, &textureName);
            texturesToCopy.append(textureName.C_Str());
        }

		if (material->GetTextureCount(aiTextureType_SPECULAR) > 0) {
			material->GetTexture(aiTextureType_SPECULAR, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
			material->GetTexture(aiTextureType_NORMALS, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_HEIGHT) > 0) {
			material->GetTexture(aiTextureType_HEIGHT, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}
    }

    if (!texturesToCopy.isEmpty()) {
        QString assetPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation) +
							Constants::ASSET_FOLDER + "/" + folderGuid;

        for (auto texture : texturesToCopy) {
			QString tex = QFileInfo(texture).isRelative()
									? QDir::cleanPath(QDir(QFileInfo(filename).absoluteDir()).filePath(texture))
									: QDir::cleanPath(texture);
            QFile::copy(tex, QDir(assetPath).filePath(QFileInfo(texture).fileName()));
        }
    }
}

void AssetView::checkForEmptyState()
{
    //if (fastGrid->containsTiles()) {
    //    ui->stackedWidget->setCurrentIndex(0);
    //    return false;
    //}

    //ui->stackedWidget->setCurrentIndex(1);
    //return true;
}

void AssetView::toggleFilterPane(bool toggle) {
    filterPane->setVisible(toggle);
}

void AssetView::spaceSplits()
{
	split->setHandleWidth(1);
	int size = this->height() / 2;
	const QList<int> sizes = { size, size };
	split->setSizes(sizes);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 1);
}

void AssetView::closeViewer()
{
    // viewer->clearScene();

    int size = this->height() / 3;
    const QList<int> sizes = { 1, size * 2 };   // 1px keeps the viewer visible so it's never fully hidden so initializegl gets called
    split->setSizes(sizes);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);

    toggleFilterPane(fastGrid->containsTiles());
}

void AssetView::clearViewer()
{
	viewer->clearScene();
}

QString AssetView::getAssetType(int id)
{
	switch (id) {
		case static_cast<int>(ModelTypes::Shader):		return "Shader";		break;
		case static_cast<int>(ModelTypes::Material):	return "Material";		break;
		case static_cast<int>(ModelTypes::Texture):		return "Texture";		break;
		case static_cast<int>(ModelTypes::Object):		return "Object";		break;
		default: return "Undefined"; break;
	}
}

AssetView::AssetView(Database *handle, QWidget *parent) : db(handle), QWidget(parent)
{
	setParent(parent);
	this->parent = parent;
	_assetView = new QListWidget;
	viewer = new AssetViewer(this);
    viewer->setDatabase(db);

    viewersWidget = new QWidget;
    viewers = new QStackedLayout;

    assetImageViewer = new QWidget;
    auto imgl = new QGridLayout;
    assetImageCanvas = new QLabel;
    imgl->addWidget(assetImageCanvas);
    imgl->setAlignment(Qt::AlignCenter);
    assetImageViewer->setLayout(imgl);

    settings = SettingsManager::getDefaultManager();
	//prefsDialog = new PreferencesDialog(this, db, settings);

	sourceGroup = new QButtonGroup;
	localAssetsButton = new QPushButton(tr(" Local Assets"));
	localAssetsButton->setCheckable(true);
	localAssetsButton->setObjectName(QStringLiteral("localAssetsButton"));
	localAssetsButton->setAccessibleName(QStringLiteral("assetsButton"));
	localAssetsButton->setIcon(QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-server-50.png")));
	localAssetsButton->setIconSize(QSize(16, 16));
	localAssetsButton->setCursor(Qt::PointingHandCursor);

	onlineAssetsButton = new QPushButton(tr(" Online Assets"));
	onlineAssetsButton->setCheckable(true);
	onlineAssetsButton->setObjectName(QStringLiteral("onlineAssetsButton"));
	onlineAssetsButton->setAccessibleName(QStringLiteral("assetsButton"));
	onlineAssetsButton->setIcon(QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-cloud-50.png")));
	onlineAssetsButton->setIconSize(QSize(16, 16));
	onlineAssetsButton->setCursor(Qt::PointingHandCursor);
    onlineAssetsButton->setDisabled(true);

	sourceGroup->addButton(localAssetsButton);
	sourceGroup->addButton(onlineAssetsButton);

	assetSource = AssetSource::LOCAL;

	connect(sourceGroup,
			static_cast<void(QButtonGroup::*)(QAbstractButton *, bool)>(&QButtonGroup::buttonToggled),
			[](QAbstractButton *button, bool checked)
	{
		QString style = checked ? "background: #3498db" : "background: #212121";
		button->setStyleSheet(style);
	});

	localAssetsButton->toggle();

	fastGrid = new AssetViewGrid(this);
	//fastGrid->installEventFilter(this);

    // gui
    _splitter = new QSplitter(this);
	_splitter->setHandleWidth(1);

    //QWidget *_filterBar;
    _navPane = new QWidget; 
    QVBoxLayout *navLayout = new QVBoxLayout;
	navLayout->setSpacing(6);
    _navPane->setLayout(navLayout);
    _navPane->setStyleSheet("background: #202020;");

	treeWidget = new QTreeWidget;
	//treeWidget->setStyleSheet("border: 1px solid red");
	treeWidget->setObjectName(QStringLiteral("TreeWidget"));
    treeWidget->setAlternatingRowColors(true);
	treeWidget->setColumnCount(2);
	treeWidget->setHeaderHidden(true);
	treeWidget->header()->setMinimumSectionSize(0);
	treeWidget->header()->setStretchLastSection(false);
	treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

	rootItem = new QTreeWidgetItem;
	rootItem->setText(0, "Asset Collections");
	rootItem->setText(1, QString());
	rootItem->setData(0, Qt::UserRole, -1);

    // list collections
    for (auto coll : db->fetchCollections()) {
        QTreeWidgetItem *treeItem = new QTreeWidgetItem;
        treeItem->setText(0, coll.name);
        treeItem->setData(0, Qt::UserRole, coll.id);
        // treeItem->setText(1, "2 items");
        //treeItem->setIcon(1, QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/world.svg")));
        rootItem->addChild(treeItem);
    }

	connect(treeWidget, &QTreeWidget::itemClicked, [this](QTreeWidgetItem *item, int column) {
		fastGrid->filterAssets(item->data(0, Qt::UserRole).toInt());
	});

	//QTreeWidgetItem *treeItem = new QTreeWidgetItem;
	//treeItem->setText(0, "Node");
	//treeItem->setIcon(1, QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/world.svg")));

	//QTreeWidgetItem *treeItem2 = new QTreeWidgetItem;
	//treeItem2->setText(0, "Node2");
	//treeItem2->setIcon(1, QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/world.svg")));

	//rootItem->addChild(treeItem);
	//rootItem->addChild(treeItem2);
	treeWidget->addTopLevelItem(rootItem);
	treeWidget->expandItem(rootItem);

    connect(this, &AssetView::refreshCollections, [this]() {
        treeWidget->clear();

        rootItem = new QTreeWidgetItem;
        rootItem->setText(0, "My Collections");
        rootItem->setText(1, QString());

        treeWidget->addTopLevelItem(rootItem);

        for (auto coll : db->fetchCollections()) {
            QTreeWidgetItem *treeItem = new QTreeWidgetItem;
            treeItem->setText(0, coll.name);
            treeItem->setData(0, Qt::UserRole, coll.id);
            rootItem->addChild(treeItem);
        }

        treeWidget->expandItem(rootItem);
    });

    navLayout->addWidget(localAssetsButton);
	navLayout->addWidget(onlineAssetsButton);
	navLayout->addSpacing(12);
	navLayout->addWidget(treeWidget);
	auto collectionButton = new QPushButton("Create Collection");
	collectionButton->setStyleSheet("font-size: 12px; padding: 8px;");
	navLayout->addWidget(collectionButton);

    connect(collectionButton, &QPushButton::pressed, [this]() {
        QDialog d;
        d.setStyleSheet("QLineEdit { font-size: 14px; background: #2f2f2f; padding: 6px; border: 0; }"
                        "QPushButton { background: #4898ff; color: white; border: 0; padding: 8px 12px; border-radius: 1px; }"
                        "QPushButton:hover { background: #51a1d6; }"
                        "QDialog { background: #1a1a1a; }");
        QHBoxLayout *l = new QHBoxLayout;
        d.setFixedWidth(350);
        d.setLayout(l);
        QLineEdit *input = new QLineEdit;
        QPushButton *accept = new QPushButton(tr("Create Collection"));

        connect(accept, &QPushButton::pressed, [&]() {
            collectionName = input->text();
            
            if (!collectionName.isEmpty()) {
                db->insertCollectionGlobal(collectionName);
                emit refreshCollections();
                QString infoText = QString("Collection Created.");
                QMessageBox::information(this, "Collection Creation Successful", infoText, QMessageBox::Ok);
                d.close();
            } else {
                QString warningText = QString("Failed to create collection. Try again.");
                QMessageBox::warning(this, "Collection Creation Failed", warningText, QMessageBox::Ok);
            }
        });

        l->addWidget(input);
        l->addWidget(accept);
        d.exec();
    });

    //QWidget *_previewPane;  
	split = new QSplitter;
	split->setHandleWidth(1);
	split->setOrientation(Qt::Vertical);

    _viewPane = new QWidget;

	auto testL = new QGridLayout;
	emptyGrid = new QWidget;
	emptyGrid->setFixedHeight(96);
	auto emptyL = new QVBoxLayout;
	testL->setMargin(0);
    testL->setSpacing(0);
	emptyL->setSpacing(0);
	auto emptyLabel = new QLabel("You have no assets in your library.");
	auto emptyIcon = new QLabel;
	emptyIcon->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
	emptyIcon->setPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-empty-box-50.png"));
	emptyLabel->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
	emptyLabel->setStyleSheet("font-size: 16px; color: #BABABA;");
	emptyL->addWidget(emptyIcon);
	emptyL->addWidget(emptyLabel);
	emptyGrid->setLayout(emptyL);

    auto meshObject = new QPushButton();
    meshObject->setAccessibleName("filterObj");
    meshObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-cube-filled-50.png")));
    meshObject->setIconSize(QSize(16, 16));
    meshObject->setStyleSheet("border-top-left-radius: 2px; border-bottom-left-radius: 2px;");

	auto typeObject = new QPushButton();
	typeObject->setAccessibleName("filterObj");
	typeObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-purchase-order-50.png")));
	typeObject->setIconSize(QSize(16, 16));

	//auto scriptObject = new QPushButton();
	//scriptObject->setAccessibleName("filterObj");
	//scriptObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-music-50.png")));
	//scriptObject->setIconSize(QSize(16, 16));
	//scriptObject->setStyleSheet("border-top-right-radius: 2px; border-bottom-right-radius: 2px;");

	auto imageObject = new QPushButton();
	imageObject->setAccessibleName("filterObj");
	imageObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-picture-50.png")));
	imageObject->setIconSize(QSize(16, 16));
	imageObject->setStyleSheet("border-top-right-radius: 2px; border-bottom-right-radius: 2px;");

	QWidget *filterGroup = new QWidget;
	auto fgL = new QHBoxLayout;
	fgL->addWidget(meshObject);
	//fgL->addWidget(typeObject);
	//fgL->addWidget(imageObject);
	//fgL->addWidget(scriptObject);
	filterGroup->setLayout(fgL);
	fgL->setMargin(0);
	fgL->setSpacing(0);

	searchTimer = new QTimer(this);
	searchTimer->setSingleShot(true);   // timer can only fire once after started

	connect(searchTimer, &QTimer::timeout, this, [this]() {
		fastGrid->searchTiles(searchTerm.toLower());
	});

	filterPane = new QWidget;
	auto filterLayout = new QHBoxLayout;

	backdropLabel = new QLabel("Backdrop: ");
	backdropColor = new QComboBox();
	backdropColor->setView(new QListView());

	backdropColor->addItem("Plain Dark", 1);
	backdropColor->addItem("Plain Light", 2);
	backdropColor->addItem("Checkered Floor", 3);
	//backdropColor->addItem("Custom Color", 4);

	filterLayout->addWidget(backdropLabel);
	filterLayout->addWidget(backdropColor);

    backdropColor->setCurrentText("Checkered Floor");

	connect(backdropColor, &QComboBox::currentTextChanged, [this](const QString &text) {
		if (text == "Plain Dark") {
			viewer->changeBackdrop(1);
		}
		else if (text == "Plain Light") {
			viewer->changeBackdrop(2);
		}
        else if (text == "Checkered Floor") {
            viewer->changeBackdrop(3);
        }
		//else if ("Custom Color") {
		//	viewer->changeBackdrop(3);
		//}
	});

	//filterLayout->addWidget(new QLabel("Filter: "));
	//filterLayout->addWidget(filterGroup);
	filterLayout->addStretch();
	filterLayout->addWidget(new QLabel("Search: "));
	le = new QLineEdit();
	le->setFixedWidth(256);
	le->setStyleSheet(
		"border: 1px solid #1E1E1E; border-radius: 1px; "
		"font-size: 12px; background: #3B3B3B; padding: 6px 4px;"
	);
	filterLayout->addWidget(le);

	connect(le, &QLineEdit::textChanged, this, [this](const QString &searchTerm) {
		this->searchTerm = searchTerm;
		searchTimer->start(100);
	});

	filterPane->setObjectName("filterPane");
	filterPane->setLayout(filterLayout);
	filterPane->setFixedHeight(48);
	filterPane->setStyleSheet(
        "#filterPane { background: #1E1E1E; border-bottom: 1px solid #111; }"
		"QLabel { font-size: 12px; margin-right: 8px; }"
		"QPushButton[accessibleName=\"filterObj\"] { border-radius: 0; padding: 10px 8px; }"
		"QComboBox { background: #222; border-radius: 1px; color: #BBB; padding: 0 12px; min-height: 30px; min-width: 72px; border: 1px solid #111;}"
		"QComboBox::drop-down { border: 0; margin: 0; padding: 0; min-height: 20px; }"
		"QComboBox::down-arrow { image: url(:/icons/down_arrow_check.png); width: 18px; height: 14px; }"
		"QComboBox::down-arrow:!enabled { image: url(:/icons/down_arrow_check_disabled.png); width: 18px; height: 14px; }"
		"QComboBox QAbstractItemView::item { min-height: 24px; selection-background-color: #404040; color: #cecece; }"
        "QComboBox QAbstractItemView { background-color: #1A1A1A; selection-background-color: #404040; border: 0; outline: none; }"
        "QComboBox QAbstractItemView::item:selected { background: #404040; }"
	);

	auto views = new QWidget;
	auto viewsL = new QVBoxLayout;
	viewsL->addWidget(emptyGrid);
	viewsL->addWidget(fastGrid);
	views->setLayout(viewsL);
    views->setStyleSheet("background: #202020");

	testL->addWidget(filterPane, 0, 0);
	testL->addWidget(views, 1, 0);
    _viewPane->setLayout(testL);

	// temp this should be checked before by emitting a signal
	fastGrid->setVisible(false);
	filterPane->setVisible(false);

	connect(fastGrid, &AssetViewGrid::gridCount, [this](int count) {
		if (count > 0) {
			filterPane->setVisible(true);
			emptyGrid->setVisible(false);
			fastGrid->setVisible(true);
		}
		else {
			filterPane->setVisible(false);
			emptyGrid->setVisible(true);
			fastGrid->setVisible(false);

            // closeViewer();
		}
	});

	// show assets
	int i = 0;
	foreach(const AssetRecord &record, db->fetchAssets()) {
		QJsonObject object;
		object["icon_url"] = "";
		object["guid"] = record.guid;
		object["name"] = record.name;
		object["type"] = record.type;
		object["collection"] = record.collection;
		object["collection_name"] = record.collection;
		object["author"] = record.author;
		object["license"] = record.license;

		auto tags = QJsonDocument::fromBinaryData(record.tags);

		QImage image;
		image.loadFromData(record.thumbnail, "PNG");

        if (image.isNull() && record.type == static_cast<int>(ModelTypes::Shader)) {
            image = QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png"));
        }

		auto sceneProperties = QJsonDocument::fromBinaryData(record.properties);

		auto gridItem = new AssetGridItem(object, image, sceneProperties.object(), tags.object());

		connect(gridItem, &AssetGridItem::addAssetItemToProject, [this](AssetGridItem *item) {
			addAssetItemToProject(item);
		});

		connect(gridItem, &AssetGridItem::changeAssetCollection, [this](AssetGridItem *item) {
			changeAssetCollection(item);
		});

		connect(gridItem, &AssetGridItem::removeAssetFromProject, [this](AssetGridItem *item) {
			removeAssetFromProject(item);
		});

		fastGrid->addTo(gridItem, i);
		i++;
	}

	//QApplication::processEvents();
	fastGrid->updateGridColumns(fastGrid->lastWidth);

    _metadataPane = new QWidget; 
	_metadataPane->setObjectName(QStringLiteral("MetadataPane"));
    _metadataPane->setStyleSheet("background: #202020");
    QVBoxLayout *metaLayout = new QVBoxLayout;
	metaLayout->setMargin(0);
	assetDropPad = new QWidget;
	assetDropPad->setAcceptDrops(true);
	assetDropPad->installEventFilter(this);
	QSizePolicy policy;
	policy.setHorizontalPolicy(QSizePolicy::Expanding);
	assetDropPad->setSizePolicy(policy);
	assetDropPad->setObjectName(QStringLiteral("assetDropPad"));
	auto assetDropPadLayout = new QVBoxLayout;
	QLabel *assetDropPadLabel = new QLabel("Drop an asset to import...");
	assetDropPadLayout->setSpacing(6);
	assetDropPadLayout->setContentsMargins(6, 6, 6, 2);
	assetDropPadLabel->setObjectName(QStringLiteral("assetDropPadLabel"));
	assetDropPadLabel->setAlignment(Qt::AlignHCenter);

	assetDropPadLayout->addWidget(assetDropPadLabel);
	QPushButton *browseButton = new QPushButton("Import Asset");
	QPushButton *downloadWorld = new QPushButton("Download Assets");

	connect(downloadWorld, &QPushButton::pressed, []() {
		QDesktopServices::openUrl(QUrl("https://www.jahfx.com/downloads/models/"));
	});

	QWidget *importButtons = new QWidget;
	auto ipbl = new QHBoxLayout;
	ipbl->setMargin(0);
	ipbl->addWidget(browseButton);
	ipbl->addWidget(downloadWorld);
	importButtons->setLayout(ipbl);

    importButtons->setStyleSheet(
        "QPushButton { background: #111; border: 1px solid black; border-radius: 1px; padding: 8px 12px; }"
    );

	assetDropPadLayout->addWidget(importButtons);

	updateAsset = new QPushButton("Update");
	updateAsset->setStyleSheet("background: #3498db");
	updateAsset->setVisible(false);

    normalize = new QPushButton("Normalize");

	addToProject = new QPushButton("Add to Project");
	addToProject->setStyleSheet(
		"QPushButton { background: #3498db; }"
		"QPushButton:hover { background-color: #4aa3de; }"
		"QPushButton:pressed { background-color: #1f80c1; }"
		"QPushButton:disabled { color: #656565; background-color: #3e3e3e; }"
	);
	addToProject->setEnabled(false);

    deleteFromLibrary = new QPushButton("Delete From Library");
	deleteFromLibrary->setStyleSheet("QPushButton { background: #E74C3C } QPushButton:disabled { color: #656565; background-color: #3e3e3e; }");
    deleteFromLibrary->setEnabled(false);

	renameModel = new QLabel("Name:");
	renameModelField = new QLineEdit();

	tagModel = new QLabel("Tags:");
	tagModelField = new QLineEdit();
	tagModelField->setPlaceholderText("(comma separated)");

	renameWidget = new QWidget;
	auto renameLayout = new QHBoxLayout;
	renameLayout->setMargin(0);
	renameLayout->setSpacing(12);
	renameLayout->addWidget(renameModel);
	renameLayout->addWidget(renameModelField);
	renameWidget->setLayout(renameLayout);
	renameWidget->setVisible(false);

	tagWidget = new QWidget;
	auto tagLayout = new QHBoxLayout;
	tagLayout->setMargin(0);
	tagLayout->setSpacing(12);
	tagLayout->addWidget(tagModel);
	tagLayout->addWidget(tagModelField);
	tagWidget->setLayout(tagLayout);
	tagWidget->setVisible(false);

	connect(fastGrid, &AssetViewGrid::selectedTileToAdd, [=](AssetGridItem *gridItem) {
		if (!gridItem->metadata.isEmpty()) {
			if (UiManager::isSceneOpen) {
				selectedGridItem = gridItem;
				addAssetItemToProject(gridItem);
				selectedGridItem = Q_NULLPTR;
			}
		}
	});

    connect(fastGrid, &AssetViewGrid::selectedTile, [&](AssetGridItem *gridItem) {
		fastGrid->deselectAll();

		renameWidget->setVisible(false);
		tagWidget->setVisible(false);
		updateAsset->setVisible(false);

		fetchMetadata(gridItem);

		if (!gridItem->metadata.isEmpty()) {

			if (UiManager::isSceneOpen) addToProject->setEnabled(true);
			deleteFromLibrary->setEnabled(true);

			renameWidget->setVisible(true);
			tagWidget->setVisible(true);
			updateAsset->setVisible(true);

			renameModelField->setText(QFileInfo(gridItem->metadata["name"].toString()).baseName());

			QString tags;
			QJsonArray children = gridItem->tags["tags"].toArray();
			for (auto childObj : children) {
				auto tag = childObj.toString();
				tags.append(tag + ", ");
			}

			tags.chop(2);

			tagModelField->setText(tags);

			// get material 
			//auto material_guid = db->getDependencyByType((int)AssetMetaType::Material, gridItem->metadata["guid"].toString());
			//auto material = db->getAssetMaterialGlobal(gridItem->metadata["guid"].toString());
			//auto materialObj = QJsonDocument::fromBinaryData(material);

            auto assetPath = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DataLocation),
                Constants::ASSET_FOLDER,
                gridItem->metadata["guid"].toString());

			// viewer->setMaterial(materialObj.object());

			QVector3D pos;
			QVector3D rot;
			int distObj;

			bool cached = false;

			if (!gridItem->sceneProperties["camera"].toObject().isEmpty()) {
				auto props = gridItem->sceneProperties["camera"].toObject();
				auto posObj = props["pos"].toObject();
				distObj = props["distFromPivot"].toDouble(5.0);
				auto rotObj = props["rot"].toObject();

				pos.setX(posObj["x"].toDouble(0));
				pos.setY(posObj["y"].toDouble(0));
				pos.setZ(posObj["z"].toDouble(0));

				rot.setX(rotObj["x"].toDouble(0));
				rot.setY(rotObj["y"].toDouble(0));
				rot.setZ(rotObj["z"].toDouble(0));

				cached = true;
			}

            if (gridItem->metadata["type"].toInt() == static_cast<int>(ModelTypes::Object) || 
                gridItem->metadata["type"].toInt() == static_cast<int>(ModelTypes::ParticleSystem)) {
                viewers->setCurrentIndex(0);

                QString path;
                // if model
                QDir dir(assetPath);
                foreach(auto &file, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files)) {
                    if (Constants::MODEL_EXTS.contains(file.suffix())) {
                        path = file.absoluteFilePath();
                        break;
                    }
                }

                if (viewer->cachedAssets.value(gridItem->metadata["guid"].toString())) {
                    viewer->addNodeToScene(viewer->cachedAssets.value(gridItem->metadata["guid"].toString()), gridItem->metadata["guid"].toString(), true, false);
                    viewer->orientCamera(pos, rot, distObj);
                }
                else {
                    viewer->loadJafModel(path, gridItem->metadata["guid"].toString(), false, true, !cached);
                    viewer->orientCamera(pos, rot, distObj);
                }
            }

            if (gridItem->metadata["type"].toInt() == static_cast<int>(ModelTypes::Material)) {
                viewers->setCurrentIndex(0);
                if (viewer->cachedAssets.value(gridItem->metadata["guid"].toString())) {
                    viewer->loadJafMaterial(gridItem->metadata["guid"].toString());
                    viewer->orientCamera(pos, rot, distObj);
                }
                else {
                    viewer->loadJafMaterial(gridItem->metadata["guid"].toString());
                    viewer->orientCamera(pos, rot, distObj);
                }
            }

            if (gridItem->metadata["type"].toInt() == static_cast<int>(ModelTypes::Shader)) {
                viewers->setCurrentIndex(0);
                if (viewer->cachedAssets.value(gridItem->metadata["guid"].toString())) {
					QMap<QString, QString> map;
                    viewer->loadJafShader(gridItem->metadata["guid"].toString(), map);
                    viewer->orientCamera(pos, rot, distObj);
                }
                else {
					QMap<QString, QString> map;
                    viewer->loadJafShader(gridItem->metadata["guid"].toString(), map);
                    viewer->orientCamera(pos, rot, distObj);
                }
            }

            if (gridItem->metadata["type"].toInt() == static_cast<int>(ModelTypes::Texture)) {
                viewers->setCurrentIndex(1);
                auto assetPath = IrisUtils::join(
                    QStandardPaths::writableLocation(QStandardPaths::DataLocation),
                    "AssetStore",
                    gridItem->metadata["guid"].toString(),
                    db->fetchAsset(gridItem->metadata["guid"].toString()).name
                );

                QPixmap image(assetPath);
                assetImageCanvas->setPixmap(image.scaledToHeight(480, Qt::SmoothTransformation));
            }

			selectedGridItem = gridItem;
			selectedGridItem->highlight(true);
		}
    });

	connect(updateAsset, &QPushButton::pressed, [this]() {
		QJsonObject tags;
		QJsonArray actualTags;

		// parse tags
		QString stringIn = tagModelField->text();
		if (!stringIn.isEmpty()) {
			std::vector<QString> commaSeparated(1);
			int commaCounter = 0;
			for (int i = 0; i<stringIn.size(); i++) {
				if (stringIn[i] == ",") {
					commaSeparated.push_back("");
					commaCounter++;
				}
				else {
					commaSeparated.at(commaCounter) += stringIn[i];
				}
			}

			for (const QString &tag : commaSeparated) {
				if (!tag.isEmpty()) actualTags.append(tag);
			}

			tags["tags"] = actualTags;
		}

		QJsonDocument tagsDoc(tags);

		auto ext = QFileInfo(selectedGridItem->metadata["full_filename"].toString()).suffix();

		db->updateAssetMetadata(
			selectedGridItem->metadata["guid"].toString(),
			renameModelField->text(),
			tagsDoc.toBinaryData()
		);

		auto metadata = selectedGridItem->metadata;
		metadata["name"] = renameModelField->text();
		metadata["full_filename"] = IrisUtils::buildFileName(renameModelField->text(), QFileInfo(selectedGridItem->metadata["full_filename"].toString()).suffix());

		selectedGridItem->updateMetadata(metadata, tags);
		fetchMetadata(selectedGridItem);
	});

	connect(addToProject, &QPushButton::pressed, [this]() {
		addAssetItemToProject(selectedGridItem);
	});

	connect(deleteFromLibrary, &QPushButton::pressed, [this]() {
		removeAssetFromProject(selectedGridItem);
	});

	connect(browseButton, &QPushButton::pressed, [=]() {
		filename = QFileDialog::getOpenFileName(this,
												"Load Mesh",
												QString(),
												"Mesh Files (*.obj *.fbx *.3ds *.jaf)");

		if (QFileInfo(filename).suffix() == "jaf") {
			importJahModel(filename);
		}
		//else {
		//	importModel(filename);
		//}
	});

	assetDropPad->setLayout(assetDropPadLayout);

    metaLayout->addWidget(assetDropPad);

	auto metadata = new QWidget;
	//metadata->setFixedHeight(256);
	auto l = new QVBoxLayout;
	l->setSpacing(12);
	//l->setMargin(0);
	QSizePolicy policy2;
	policy2.setVerticalPolicy(QSizePolicy::Preferred);
	policy2.setHorizontalPolicy(QSizePolicy::Preferred);
	metadataMissing = new QLabel("Nothing selected...");
	metadataMissing->setAlignment(Qt::AlignCenter);
	metadataMissing->setStyleSheet("padding: 12px; text-align: center");
	metadataMissing->setSizePolicy(policy2);
	metadataName = new QLabel("Name: ");
	metadataName->setSizePolicy(policy2);
	metadataType = new QLabel("Type: ");
	metadataType->setSizePolicy(policy2);
	metadataAuthor = new QLabel("Author: ");
	metadataAuthor->setSizePolicy(policy2);
	metadataLicense = new QLabel("License: ");
	metadataLicense->setSizePolicy(policy2);
	metadataTags = new QLabel("Tags: ");
	metadataTags->setSizePolicy(policy2);
	metadataVisibility = new QLabel("Public: ");
	metadataVisibility->setSizePolicy(policy2);
	metadataCollection = new QLabel("Collection: ");
	metadataCollection->setSizePolicy(policy2);
    metadataVisibility->setVisible(false);

	metadataName->setVisible(false);
	metadataType->setVisible(false);
	metadataAuthor->setVisible(false);
	metadataLicense->setVisible(false);
	metadataTags->setVisible(false);

    changeMetaCollection = new QPushButton(tr("change"));
    changeMetaCollection->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    changeMetaCollection->setStyleSheet("font-size: 8px; color: green; padding: 0; background: transparent; border: 0");
    metadataLayout = new QHBoxLayout;
    metadataLayout->setMargin(0);
    metadataLayout->setSpacing(12);
    metadataLayout->addWidget(metadataCollection);
    // metadataLayout->addWidget(changeMetaCollection);
    metadataLayout->addStretch();
    metadataWidget = new QWidget;
    metadataWidget->setLayout(metadataLayout);
	metadataWidget->setVisible(false);

	l->addWidget(metadataMissing);

	l->addWidget(renameWidget);
	l->addWidget(tagWidget);

	//l->addWidget(metadataName);
	l->addWidget(metadataType);
	l->addWidget(metadataVisibility);
	l->addWidget(metadataAuthor);
	l->addWidget(metadataLicense);
	//l->addWidget(metadataTags);
	l->addWidget(metadataWidget);
	l->addWidget(updateAsset);

	metadata->setLayout(l);
	//metadata->setStyleSheet("QLabel { font-size: 12px; }");
	auto header = new QLabel("Asset Metadata");
	header->setAlignment(Qt::AlignCenter);
	header->setStyleSheet("border-top: 1px solid black; border-bottom: 1px solid black; text-align: center; padding: 12px; background: #1A1A1A");
	metaLayout->addWidget(header);
	metaLayout->addWidget(metadata);

	metaLayout->addStretch();

	auto projectSpecific = new QWidget;
	auto ll = new QVBoxLayout;
	// ll->addWidget(normalize);
	ll->addWidget(addToProject);
	ll->addWidget(deleteFromLibrary);
	projectSpecific->setLayout(ll);
	metaLayout->addWidget(projectSpecific);

    _metadataPane->setLayout(metaLayout);

    viewers->addWidget(viewer);
    viewers->addWidget(assetImageViewer);
    viewersWidget->setLayout(viewers);

	//split->addWidget(viewer);
	split->addWidget(viewersWidget);
	split->addWidget(_viewPane);

    _splitter->addWidget(_navPane);
    _splitter->addWidget(split);
    _splitter->addWidget(_metadataPane);

    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 3);
    _splitter->setStretchFactor(2, 1);
    
    QGridLayout *layout = new QGridLayout;
	layout->setMargin(0);
    layout->addWidget(_splitter);
    setLayout(layout);

	setStyleSheet(
		"*							{ color: #EEE; }"
        "QPushButton				{ background: #404040; border-radius: 2px; padding: 8px 12px; }"
		"QSplitter					{ background: #2E2E2E; } QSplitter:handle { background: black; }"
		"#localAssetsButton			{ text-align: left; padding: 12px; }"
		"#onlineAssetsButton		{ text-align: left; padding: 12px; }"
        "QPushButton[accessibleName=\"assetsButton\"]:disabled { color: #444; }"
		"#assetDropPad				{}"
		"#assetDropPadLabel			{ border: 4px dashed #111; border-radius: 4px; "
		"							  padding: 48px 36px; margin: 0; }"
        "#assetDropPad, #MetadataPane QPushButton	{ padding: 8px 12px; }"
		"QLineEdit					{ border: 1px solid #1E1E1E; border-radius: 2px; background: #3B3B3B; }"
		"#assetDropPad QLabel		{}"
        "QTreeView, QTreeWidget { show-decoration-selected: 1; border: 0; alternate-background-color: #252525;"
        "                         selection-background-color: #404040; color: #EEE; background: #202020;"
        "                         paint-alternating-row-colors-for-empty-area: 1; outline: none; }"
		//"QTreeWidget::branch { background-color: #202020; }"
        "QTreeView::branch:open { image: url(:/icons/expand_arrow_open.png); }"
        "QTreeView::branch:closed:has-children { image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040;"
		"							  background: #404040; outline: none; padding: 5px 0; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item { padding: 5px 0; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }"
        "QComboBox { background: #1A1A1A; border : 0; }"
	);
}

QString importProjectNameAV;
int on_extract_entry_av(const char *filename, void *arg) {
	QFileInfo fInfo(filename);
	if (fInfo.suffix() == "db") importProjectNameAV = fInfo.baseName();
	return 0;
}

void AssetView::importJahModel(const QString &fileName)
{
    QFileInfo entryInfo(fileName);

    auto assetPath = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::DataLocation),
        "AssetStore"
    );

    // create a temporary directory and extract our project into it
    // we need a sure way to get the project name, so we have to extract it first and check the blob
    QTemporaryDir temporaryDir;
    if (temporaryDir.isValid()) {
        zip_extract(entryInfo.absoluteFilePath().toStdString().c_str(),
            temporaryDir.path().toStdString().c_str(),
            Q_NULLPTR, Q_NULLPTR
        );

        QFile f(QDir(temporaryDir.path()).filePath(".manifest"));

        if (!f.exists()) {
            QMessageBox::warning(
                this,
                "Incompatible Asset format",
                "This asset was made with a deprecated version of Jahshaka\n"
                "You can extract the contents manually and try importing as regular assets.",
                QMessageBox::Ok
            );

            return;
        }

        if (!f.open(QFile::ReadOnly | QFile::Text)) return;
        QTextStream in(&f);
        const QString jafString = in.readLine();
        f.close();

        ModelTypes jafType = ModelTypes::Undefined;

        if (jafString == "bundle") {
            importJahBundle(fileName);
            return;
        }

        if (jafString == "object") {
            jafType = ModelTypes::Object;
        }
        else if (jafString == "texture") {
            jafType = ModelTypes::Texture;
        }
        else if (jafString == "material") {
            jafType = ModelTypes::Material;
        }
        else if (jafString == "shader") {
            jafType = ModelTypes::Shader;
        }
        else if (jafString == "particle_system") {
            jafType = ModelTypes::ParticleSystem;
        }

        QVector<AssetRecord> records;

        QMap<QString, QString> guidCompareMap;
        QString guid = db->importAsset(jafType,
                            QDir(temporaryDir.path()).filePath("asset.db"),
                            QMap<QString, QString>(),
                            guidCompareMap,
                            records);

        const QString assetFolder = QDir(assetPath).filePath(guid);
        QDir().mkpath(assetFolder);

        QString assetsDir = QDir(temporaryDir.path()).filePath("assets");
        QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files);

        QStringList fileNames;
        while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

        jafType = ModelTypes::Undefined;

        QString placeHolderGuid = GUIDManager::generateGUID();

        for (const auto &file : fileNames) {
            QFileInfo fileInfo(file);
            QString fileToCopyTo = IrisUtils::join(assetFolder, fileInfo.fileName());
            bool copyFile = QFile::copy(fileInfo.absoluteFilePath(), fileToCopyTo);
        }

        if (jafString == "material") {
            viewers->setCurrentIndex(0);
            renameModelField->setText(QFileInfo(filename).baseName());
            viewer->loadJafMaterial(guid);
            addToJahLibrary(filename, guid, true);
        }

        if (jafString == "shader") {
            viewers->setCurrentIndex(0);
            renameModelField->setText(QFileInfo(filename).baseName());
            viewer->loadJafShader(guid, guidCompareMap);
            addToJahLibrary(filename, guid, true);
        }

        if (jafString == "texture") {
            renameModelField->setText(QFileInfo(filename).baseName());

            {
                viewers->setCurrentIndex(1);
                auto assetPath = IrisUtils::join(
                    QStandardPaths::writableLocation(QStandardPaths::DataLocation),
                    "AssetStore",
                    guid,
                    db->fetchAsset(guid).name
                );

                QPixmap image(assetPath);
                assetImageCanvas->setPixmap(image.scaledToHeight(480, Qt::SmoothTransformation));
            }

            addToJahLibrary(filename, guid, true);
        }

        if (jafString == "object") {
            viewers->setCurrentIndex(0);
            // Open the asset
            QString path;
            // if model
            QDir dir(assetFolder);
            foreach(auto &file, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files)) {
                if (Constants::MODEL_EXTS.contains(file.suffix())) {
                    path = file.absoluteFilePath();
                    break;
                }
            }

            renameModelField->setText(QFileInfo(filename).baseName());
            viewer->loadJafModel(path, guid);
            addToJahLibrary(filename, guid, true);
        }
    }
}

void AssetView::importJahBundle(const QString &fileName)
{
    QFileInfo entryInfo(fileName);

    auto assetPath = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::DataLocation),
        "AssetStore"
    );

    // create a temporary directory and extract our project into it
    // we need a sure way to get the project name, so we have to extract it first and check the blob
    QTemporaryDir temporaryDir;
    temporaryDir.setAutoRemove(false);
    if (temporaryDir.isValid()) {
        zip_extract(entryInfo.absoluteFilePath().toStdString().c_str(),
            temporaryDir.path().toStdString().c_str(),
            Q_NULLPTR, Q_NULLPTR
        );

        QFile f(QDir(temporaryDir.path()).filePath(".manifest"));

        if (!f.exists()) {
            QMessageBox::warning(
                this,
                "Incompatible Asset format",
                "This asset was made with a deprecated version of Jahshaka\n"
                "You can extract the contents manually and try importing as regular assets.",
                QMessageBox::Ok
            );

            return;
        }

        QStringList lines;
        if (!f.open(QFile::ReadOnly | QFile::Text)) return;
        QTextStream in(&f);

        while (!in.atEnd()) {
            QString line = in.readLine();
            lines << line;
        }
        f.close();

        const QString jafString = lines.first();
        lines.pop_front();

        QVector<AssetRecord> records;

        QMap<QString, QString> guidCompareMap;
        QString guid = db->importAssetBundle(
            QDir(temporaryDir.path()).filePath("asset.db"),
            QMap<QString, QString>(),
            guidCompareMap,
            records
        );

        QMap<QString, QString> guidsToReplace;

        QMap<QString, QString>::const_iterator ptIter;
        for (ptIter = guidCompareMap.constBegin(); ptIter != guidCompareMap.constEnd(); ++ptIter) {
            if (lines.contains(ptIter.key())) {
                guidsToReplace.insert(ptIter.key(), ptIter.value());
            }
        }

        for (ptIter = guidsToReplace.constBegin(); ptIter != guidsToReplace.constEnd(); ++ptIter) {
            QString assetsDir = QDir(QDir(temporaryDir.path()).filePath("assets")).filePath(ptIter.key());
            QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden);

            QStringList fileNames;
            while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

            const QString assetFolder = QDir(assetPath).filePath(ptIter.value());
            QDir().mkpath(assetFolder);

            for (const auto &file : fileNames) {
                QFileInfo fileInfo(file);
                QString fileToCopyTo = IrisUtils::join(assetFolder, fileInfo.fileName());
                bool copyFile = QFile::copy(fileInfo.absoluteFilePath(), fileToCopyTo);
            }
        }
    }
}

void AssetView::importModel(const QString &filename, bool jfx)
{
	if (!filename.isEmpty()) {
		renameModelField->setText(QFileInfo(filename).baseName());
		viewer->loadModel(filename);
		addToLibrary(jfx);
	}
}

void AssetView::addToJahLibrary(const QString fileName, const QString guid, bool jfx)
{
    QJsonObject tags;
    QJsonArray actualTags;

    QFileInfo fInfo(filename);
    QJsonObject object;
    object["icon_url"] = "";
    object["name"] = QFileInfo(fileName).baseName(); // renameModelField->text();

    //auto thumbnail = viewer->takeScreenshot(512, 512);

    auto bytes = db->fetchAsset(guid).thumbnail;
    QImage thumbnail;
    if (!thumbnail.loadFromData(bytes, "PNG")) {
        //thumbnail = viewer->takeScreenshot(512, 512);
        //db->updateAssetThumbnail(guid, bytes);
    }

    db->updateAssetProperties(guid, QJsonDocument(viewer->getSceneProperties()).toBinaryData());
    //db->updateAssetThumbnail(guid, bytes);

    object["type"] = db->fetchAsset(guid).type;

    if (object["type"].toInt() == static_cast<int>(ModelTypes::Shader)) {
        thumbnail = QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png"));
    }

    if (object["type"].toInt() == static_cast<int>(ModelTypes::ParticleSystem)) {
        thumbnail = QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-ps.png"));
    }

    object["guid"] = guid;

    auto gridItem = new AssetGridItem(object, thumbnail, viewer->getSceneProperties(), tags);

    connect(gridItem, &AssetGridItem::addAssetItemToProject, [this](AssetGridItem *item) {
		addAssetItemToProject(item);
    });

    connect(gridItem, &AssetGridItem::changeAssetCollection, [this](AssetGridItem *item) {
        changeAssetCollection(item);
    });

    connect(gridItem, &AssetGridItem::removeAssetFromProject, [this](AssetGridItem *item) {
        removeAssetFromProject(item);
    });

    viewer->cacheCurrentModel(guid);

    fastGrid->addTo(gridItem, 0, true);
    QApplication::processEvents();
    fastGrid->updateGridColumns(fastGrid->lastWidth);

    renameWidget->setVisible(true);
    tagWidget->setVisible(true);
    updateAsset->setVisible(true);
}

void AssetView::addToLibrary(bool jfx)
{
	//bool canAdd = db->isAuthorInfoPresent();
	QJsonObject tags;
	QJsonArray actualTags;

	// parse tags
	//QString stringIn = tagModelField->text();
	//if (!stringIn.isEmpty()) {
	//	std::vector<QString> commaSeparated(1);
	//	int commaCounter = 0;
	//	for (int i = 0; i<stringIn.size(); i++) {
	//		if (stringIn[i] == ",") {
	//			commaSeparated.push_back("");
	//			commaCounter++;
	//		}
	//		else {
	//			commaSeparated.at(commaCounter) += stringIn[i];
	//		}
	//	}

	//	for (const QString &tag : commaSeparated) {
	//		if (!tag.isEmpty()) actualTags.append(tag);
	//	}

	//	tags["tags"] = actualTags;
	//}

	//if (canAdd) {
		QFileInfo fInfo(filename);
		QJsonObject object;
		object["icon_url"] = "";
		object["name"] = QFileInfo(filename).baseName(); // renameModelField->text();

		auto assetSnapshot = viewer->takeScreenshot(512, 512);

		QJsonDocument tagsDoc(tags);

		// maybe actually check if Object?
		QString guid;
		if (jfx) {
			guid = db->createAssetEntry(
                       GUIDManager::generateGUID(),
                       QFileInfo(filename).fileName(),
				       static_cast<int>(ModelTypes::Object),
                       QString(),
                       QString(),
                       "JahFX",
                       AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(assetSnapshot)),
                       QJsonDocument(viewer->getSceneProperties()).toBinaryData(),
                       tagsDoc.toBinaryData(),
                       QJsonDocument(viewer->getMaterial()).toBinaryData()
                   );
		}
		else {
            guid = db->createAssetEntry(
                GUIDManager::generateGUID(),
                QFileInfo(filename).fileName(),
                static_cast<int>(ModelTypes::Object),
                QString(),
                QString(),
                QString(),
                AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(assetSnapshot)),
                QJsonDocument(viewer->getSceneProperties()).toBinaryData(),
                tagsDoc.toBinaryData(),
                QJsonDocument(viewer->getMaterial()).toBinaryData()
            );
		}

		object["guid"] = guid;
		object["type"] = db->fetchAsset(guid).type; // model?
		object["full_filename"] = IrisUtils::buildFileName(guid, fInfo.suffix());
		if (jfx) {
			object["author"] = "JahFX";// db->getAuthorName();
		}
		else {
			object["author"] = "";// db->getAuthorName();
		}
		object["license"] = "CCBY";

		Globals::assetNames.insert(guid, object["name"].toString());

		auto assetPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + Constants::ASSET_FOLDER;

		if (!QDir(QDir(assetPath).filePath(guid)).exists()) {
			QDir().mkdir(QDir(assetPath).filePath(guid));
			bool copyFile = QFile::copy(filename,
				QDir(QDir(assetPath).filePath(guid)).filePath(
					IrisUtils::buildFileName(guid, fInfo.suffix().toLower()))
			);
		}

		copyTextures(guid);

		//auto material_guid = db->insertMaterialGlobal(QFileInfo(filename).baseName() + "_material", guid, QJsonDocument(viewer->getMaterial()).toBinaryData());
		//db->insertGlobalDependency(static_cast<int>(ModelTypes::Material), guid, material_guid);

		auto gridItem = new AssetGridItem(object, assetSnapshot, viewer->getSceneProperties(), tags);

		connect(gridItem, &AssetGridItem::addAssetItemToProject, [this](AssetGridItem *item) {
			addAssetItemToProject(item);
		});

		connect(gridItem, &AssetGridItem::changeAssetCollection, [this](AssetGridItem *item) {
			changeAssetCollection(item);
		});

		connect(gridItem, &AssetGridItem::removeAssetFromProject, [this](AssetGridItem *item) {
			removeAssetFromProject(item);
		});

		viewer->cacheCurrentModel(guid);

		fastGrid->addTo(gridItem, 0, true);
		QApplication::processEvents();
		fastGrid->updateGridColumns(fastGrid->lastWidth);

		renameWidget->setVisible(true);
		tagWidget->setVisible(true);
		updateAsset->setVisible(true);
		//addToLibrary->setVisible(false);
	//}
	//else {
	//	auto option = QMessageBox::question(this,
	//		"No Author!", "There is no author set, would you like to set a name now?\n"
	//		"Without it you will not be able to import assets.\n\n"
	//		"Enter a valid name in the Author field and save.",
	//		QMessageBox::Yes | QMessageBox::No);

	//	if (option == QMessageBox::Yes) {
	//		prefsDialog->exec();
	//	}
	//	else {
	//		QMessageBox::warning(this, "Failed to add asset!", "Nothing was done.", QMessageBox::Ok);
	//	}
	//}
}

void AssetView::fetchMetadata(AssetGridItem *widget)
{
	if (!widget->metadata.isEmpty()) {
		metadataMissing->setVisible(false);

		//metadataName->setVisible(true);
		metadataType->setVisible(true);
		metadataVisibility->setVisible(true);
		metadataAuthor->setVisible(true);
		metadataLicense->setVisible(true);
		//metadataTags->setVisible(true);
        metadataWidget->setVisible(true);

		//metadataName->setText("Name: " + QFileInfo(widget->metadata["name"].toString()).baseName());
		metadataType->setText("Type: " + getAssetType(widget->metadata["type"].toInt()));
		QString pub = widget->metadata["is_public"].toBool() ? "true" : "false";
		metadataVisibility->setText("Public: " + pub);
		metadataAuthor->setText("Author: " + widget->metadata["author"].toString());
		metadataLicense->setText("License: " + widget->metadata["license"].toString());
		
		//QString tags;

		//QJsonArray children = widget->tags["tags"].toArray();

		//for (auto childObj : children) {
		//	auto tag = childObj.toString();
		//	tags.append(tag + " ");
		//}

		//metadataTags->setText("Tags: " + tags);
		metadataCollection->setText("Collection: " + widget->metadata["collection_name"].toString());
	}
	else {
		metadataMissing->setVisible(true);

		addToProject->setEnabled(false);
		deleteFromLibrary->setEnabled(false);

		//metadataName->setVisible(false);
		metadataType->setVisible(false);
		metadataVisibility->setVisible(false);
		metadataAuthor->setVisible(false);
		metadataLicense->setVisible(false);
		//metadataTags->setVisible(false);
        metadataWidget->setVisible(false);
	}
}

void AssetView::addAssetItemToProject(AssetGridItem *item)
{
	//auto rx = _navPane->rect().x() + viewer->rect().x();
	//auto ry = _navPane->rect().y() + viewer->rect().y();
	//auto rw = _navPane->rect().width() + viewer->rect().width();
	//auto rh = viewer->rect().height() + 32;

	//auto endRect = QRect(parent->pos().x(), parent->pos().y(), rw, rh);
	Toast *t = new Toast(this);
	t->showToast(
		"Asset Added To Project",
		QString("%1 has been added successfully to the open project.").arg(item->metadata["name"].toString()),
		0, parent->pos(), QRect()
	);

	// get the current project working directory
	auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), Constants::PROJECT_FOLDER);
	auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();
	auto pDir = IrisUtils::join(defaultProjectDirectory, Globals::project->getProjectGuid());

	QString guid = item->metadata["guid"].toString();
	int assetType = item->metadata["type"].toInt();

	auto assetsDir = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DataLocation), Constants::ASSET_FOLDER, guid);

    const int aType = db->fetchAsset(guid).type;

    QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files);

    QStringList fileNames;
    while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

    // Create a pair that holds the original name and the new name (if any)
    QVector<QPair<QString, QString>> files;	/* original x new */

    ModelTypes jafType = ModelTypes::Undefined;

    QString placeHolderGuid = GUIDManager::generateGUID();

    for (const auto &file : fileNames) {
        QFileInfo fileInfo(file);
		ModelTypes jafType = AssetHelper::getAssetTypeFromExtension(fileInfo.suffix().toLower());

        QString pathToCopyTo = Globals::project->getProjectFolder();
        QString fileToCopyTo = IrisUtils::join(pathToCopyTo, fileInfo.fileName());

        int increment = 1;
        QFileInfo checkFile(fileToCopyTo);
        // If we encounter the same file, make a duplicate...
        QString newFileName = fileInfo.fileName();

        while (checkFile.exists()) {
            QString newName = fileInfo.baseName() + " " + QString::number(increment++);
            checkFile = QFileInfo(IrisUtils::buildFileName(
                IrisUtils::join(pathToCopyTo, newName), fileInfo.suffix())
            );
            newFileName = checkFile.fileName();
			fileToCopyTo = checkFile.absoluteFilePath();
        }

        files.push_back(QPair<QString, QString>(file, fileToCopyTo));
        
		bool copyFile = QFile::copy(file, fileToCopyTo);

		QFileInfo newFileInfo(fileToCopyTo);

        if (jafType == ModelTypes::File) {
            auto assetFile = new AssetFile;
            assetFile->fileName = newFileInfo.fileName();
            assetFile->assetGuid = placeHolderGuid;
            assetFile->path = fileToCopyTo;
            AssetManager::addAsset(assetFile);
        }

		if (jafType == ModelTypes::Texture) {
			auto assetTexture = new AssetTexture;
			assetTexture->fileName = newFileInfo.fileName();
			assetTexture->assetGuid = placeHolderGuid;
			assetTexture->path = fileToCopyTo;
			AssetManager::addAsset(assetTexture);
		}

        if (jafType == ModelTypes::Mesh) {
            this->viewer->makeCurrent();
            auto ssource = new iris::SceneSource();
            // load mesh as scene
            auto node = iris::MeshNode::loadAsSceneFragment(
				fileToCopyTo,
                [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
            {
                auto mat = iris::CustomMaterial::create();
                if (mesh->hasSkeleton())
                    mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
                else
                    mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

                return mat;
            }, ssource);
            this->viewer->doneCurrent();

            QVariant variant = QVariant::fromValue(node);
            auto nodeAsset = new AssetNodeObject;
			nodeAsset->fileName = newFileInfo.fileName();
			nodeAsset->path = fileToCopyTo;
            nodeAsset->assetGuid = placeHolderGuid;	/* temp guid */
            nodeAsset->setValue(variant);
            AssetManager::addAsset(nodeAsset);
        }
    }

    QMap<QString, QString> newNames;	/* original x new */
    for (const auto &file : files) {
        newNames.insert(
            QFileInfo(file.first).fileName(),
            QFileInfo(file.second).fileName()
        );
    }

    // We can discern most types from their extension, we don't store material files so we use the manifest
    if (aType == static_cast<int>(ModelTypes::Material)) {
        jafType = ModelTypes::Material;
    }
    else if (aType == static_cast<int>(ModelTypes::Object)) {
        jafType = ModelTypes::Object;
    }
	else if (aType == static_cast<int>(ModelTypes::Texture)) {
		jafType = ModelTypes::Texture;
	}
    else if (aType == static_cast<int>(ModelTypes::Shader)) {
        jafType = ModelTypes::Shader;
    }
    else if (aType == static_cast<int>(ModelTypes::ParticleSystem)) {
        jafType = ModelTypes::ParticleSystem;
    }
    else {
        // Default to files since we know what archives can contain
        jafType = ModelTypes::File;
    }

    QVector<AssetRecord> oldAssetRecords;

    QString guidReturned = db->copyAsset(
        jafType, guid, newNames,
        oldAssetRecords, Globals::project->getProjectGuid()
    );

    for (auto &asset : AssetManager::getAssets()) {
        if (asset->type == ModelTypes::File) {
            for (const auto &record : oldAssetRecords) {
                if (record.name == asset->fileName) {
                    asset->assetGuid = record.guid;
                }
            }
        }
    }

    if (jafType == ModelTypes::Texture) {
        for (auto &asset : AssetManager::getAssets()) {
            if (asset->assetGuid == placeHolderGuid && asset->type == ModelTypes::Texture) {
                asset->assetGuid = guidReturned;
            }
        }
    }

    if (jafType == ModelTypes::Shader) {
        QJsonDocument matDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guidReturned));
        QJsonObject shaderDefinition = matDoc.object();

        auto assetShader = new AssetShader;
        assetShader->assetGuid = guidReturned;
        assetShader->fileName = db->fetchAsset(guidReturned).name;
        assetShader->setValue(QVariant::fromValue(shaderDefinition));
        AssetManager::addAsset(assetShader);
    }
    else {
        for (const auto &asset : oldAssetRecords) {
            if (asset.type == static_cast<int>(ModelTypes::Shader)) {
                QJsonDocument matDoc = QJsonDocument::fromBinaryData(asset.asset);
                QJsonObject shaderDefinition = matDoc.object();

                auto assetShader = new AssetShader;
                assetShader->assetGuid = asset.guid;
                assetShader->fileName = asset.name;
                assetShader->setValue(QVariant::fromValue(shaderDefinition));
                AssetManager::addAsset(assetShader);
            }
        }
    }

    if (jafType == ModelTypes::ParticleSystem) {
        QJsonDocument matDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guidReturned));
        QJsonObject shaderDefinition = matDoc.object();

        auto assetShader = new AssetParticleSystem;
        assetShader->assetGuid = guidReturned;
        assetShader->fileName = db->fetchAsset(guidReturned).name;
        assetShader->setValue(QVariant::fromValue(shaderDefinition));
        AssetManager::addAsset(assetShader);
    }

    if (jafType == ModelTypes::Object) {
        for (auto &asset : AssetManager::getAssets()) {
            if (asset->assetGuid == placeHolderGuid && asset->type == ModelTypes::Object) {
                asset->assetGuid = guidReturned;
                auto node = asset->getValue().value<iris::SceneNodePtr>();
                auto material = db->fetchAssetData(guidReturned);
                auto materialObj = QJsonDocument::fromBinaryData(material);
                AssetHelper::updateNodeMaterial(node, materialObj.object());
            }
        }
    }

    if (jafType == ModelTypes::Material) {
        QJsonDocument matDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guidReturned));
        QJsonObject matObject = matDoc.object();
        iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();

        QFileInfo shaderFile;

        QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
        while (it.hasNext()) {
            it.next();
            if (it.key() == matObject["guid"].toString()) {
                shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
                break;
            }
        }

        if (shaderFile.exists()) {
            material->generate(shaderFile.absoluteFilePath());
        }
        else {
            for (auto asset : AssetManager::getAssets()) {
                if (asset->type == ModelTypes::Shader) {
                    if (asset->assetGuid == matObject["guid"].toString()) {
                        auto def = asset->getValue().toJsonObject();
                        auto vertexShader = def["vertex_shader"].toString();
                        auto fragmentShader = def["fragment_shader"].toString();
                        for (auto asset : AssetManager::getAssets()) {
                            if (asset->type == ModelTypes::File) {
                                if (vertexShader == asset->assetGuid) vertexShader = asset->path;
                                if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
                            }
                        }
                        def["vertex_shader"] = vertexShader;
                        def["fragment_shader"] = fragmentShader;
						material->setMaterialDefinition(def);
                        material->generate(def);
                    }
                }
            }
        }

        for (const auto &prop : material->properties) {
            if (prop->type == iris::PropertyType::Color) {
                QColor col;
                col.setNamedColor(matObject.value(prop->name).toString());
                material->setValue(prop->name, col);
            }
            else if (prop->type == iris::PropertyType::Texture) {
                QString materialName = db->fetchAsset(matObject.value(prop->name).toString()).name;
                QString textureStr = IrisUtils::join(
                    Globals::project->getProjectFolder(), materialName
                );
                material->setValue(prop->name, !materialName.isEmpty() ? textureStr : QString());
            }
            else {
                material->setValue(prop->name, QVariant::fromValue(matObject.value(prop->name)));
            }
        }

        auto assetMat = new AssetMaterial;
        assetMat->assetGuid = guidReturned;
        assetMat->setValue(QVariant::fromValue(material));
        AssetManager::addAsset(assetMat);
    }
}

void AssetView::changeAssetCollection(AssetGridItem *item)
{
	QDialog d;
	d.setStyleSheet("QLineEdit { font-size: 14px; background: #2f2f2f; padding: 6px; border: 0; }"
		"QComboBox { background: #4D4D4D; color: #BBB; padding: 6px; border: 0; }"
		"QComboBox::drop-down { border : 0; }"
		"QComboBox::down-arrow { image: url(:/icons/down_arrow_check.png); width: 18px; height: 14px; }"
		"QComboBox::down-arrow:!enabled { image: url(:/icons/down_arrow_check_disabled.png); width: 18px; height: 14px; }"
		"QPushButton { background: #4898ff; color: white; border: 0; padding: 8px 12px; border-radius: 1px; }"
		"QPushButton:hover { background: #555; }"
		"QDialog { background: #1A1A1A; }");

	QHBoxLayout *l = new QHBoxLayout;
	d.setFixedWidth(350);
	d.setLayout(l);
	QComboBox *input = new QComboBox;
	QPushButton *accept = new QPushButton(tr("Change Collection"));
	l->addWidget(input);
	l->addWidget(accept);

	for (auto item : db->fetchCollections()) {
		input->addItem(item.name, QVariant(item.id));
	}

	auto guid = item->metadata["guid"].toString();

	connect(accept, &QPushButton::pressed, [&]() {
		if (db->switchAssetCollection(input->currentData().toInt(), guid)) {
			QString infoText = QString("Collection Changed.");
			QMessageBox::information(this, "Collection Change Successful", infoText, QMessageBox::Ok);
			item->metadata["collection"] = input->currentData().toInt();
			item->metadata["collection_name"] = input->currentText();

			if (!treeWidget->selectedItems().isEmpty()) {
				fastGrid->filterAssets(treeWidget->currentItem()->data(0, Qt::UserRole).toInt());
			}
			else {
				fastGrid->filterAssets(-1);
			}
		}
		else {
			QString warningText = QString("Failed to change collection. Try again.");
			QMessageBox::warning(this, "Collection Change Failed", warningText, QMessageBox::Ok);
		}

		d.close();
	});

	d.exec();
}

void AssetView::removeAssetFromProject(AssetGridItem *item)
{
	auto assetPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + Constants::ASSET_FOLDER;

	auto option = QMessageBox::question(this,
	    "Deleting Asset", "Are you sure you want to delete this asset?",
	    QMessageBox::Yes | QMessageBox::Cancel);

	if (option == QMessageBox::Yes) {
	    if (IrisUtils::removeDir(QDir(assetPath).filePath(item->metadata["guid"].toString()))) {
	        fastGrid->deleteTile(item);
			// if the item is being used soft delete it
			//db->deleteAsset(item->metadata["guid"].toString());
            db->deleteAssetAndDependencies(item->metadata["guid"].toString());

			item->metadata = QJsonObject();
			renameWidget->setVisible(false);
			tagWidget->setVisible(false);
			updateAsset->setVisible(false);

			fetchMetadata(item);
	        clearViewer();
	    }
	    else {
	        QMessageBox::warning(this, "Delete Failed!", "Failed to remove asset, please try again!", QMessageBox::Ok);
	    }
	}
}

AssetView::~AssetView()
{
    
}
