#include "assetview.h"
#include "irisgl/src/core/irisutils.h"

#include "irisgl/src/graphics/mesh.h"

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
#include <QBuffer>>
#include <QTreeWidget>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include "../globals.h"
#include "../constants.h"
#include "../core/settingsmanager.h"
#include "../core/database/database.h"
#include "../uimanager.h"
#include "assetviewgrid.h"
#include "assetgriditem.h"
#include "assetviewer.h"

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

				importModel(list.front());

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

QString AssetView::getAssetType(int id)
{
	switch (id) {
		case static_cast<int>(AssetMetaType::Shader):		return "Shader";		break;
		case static_cast<int>(AssetMetaType::Material):		return "Material";		break;
		case static_cast<int>(AssetMetaType::Texture):		return "Texture";		break;
		case static_cast<int>(AssetMetaType::Video):		return "Video";			break;
		case static_cast<int>(AssetMetaType::Cubemap):		return "Cubemap";		break;
		case static_cast<int>(AssetMetaType::Object):		return "Object";		break;
		case static_cast<int>(AssetMetaType::SoundEffect):	return "SoundEffect";	break;
		case static_cast<int>(AssetMetaType::Music):		return "Music";			break;
		default: return "Undefined"; break;
	}
}

AssetView::AssetView(Database *handle, QWidget *parent) : db(handle), QWidget(parent)
{
	_assetView = new QListWidget;
	viewer = new AssetViewer(this);

    settings = SettingsManager::getDefaultManager();

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

	treeWidget = new QTreeWidget;
	//treeWidget->setStyleSheet("border: 1px solid red");
	treeWidget->setObjectName(QStringLiteral("treeWidget"));
	treeWidget->setColumnCount(2);
	treeWidget->setHeaderHidden(true);
	treeWidget->header()->setMinimumSectionSize(0);
	treeWidget->header()->setStretchLastSection(false);
	treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

	rootItem = new QTreeWidgetItem;
	rootItem->setText(0, "My Collections");
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
	collectionButton->setStyleSheet("font-size: 12px; font-weight: bold; padding: 8px;");
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
	//filterLayout->addWidget(new QLabel("Filter: "));
	//filterLayout->addWidget(filterGroup);
	filterLayout->addStretch();
	filterLayout->addWidget(new QLabel("Search: "));
	le = new QLineEdit();
	le->setFixedWidth(256);
	le->setStyleSheet(
		"border: 1px solid #1E1E1E; border - radius: 2px; "
		"font-size: 12px; font-weight: bold; background: #3B3B3B; padding: 6px 4px;"
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
		"#filterPane { border-bottom: 1px solid #111; }"
		"QLabel { font-size: 12px; font-weight: bold; margin-right: 8px; }"
		"QPushButton[accessibleName=\"filterObj\"] { border-radius: 0; padding: 10px 8px; }"
	);

	auto views = new QWidget;
	auto viewsL = new QVBoxLayout;
	viewsL->addWidget(emptyGrid);
	viewsL->addWidget(fastGrid);
	views->setLayout(viewsL);

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

            closeViewer();
		}
	});

	// show assets
	int i = 0;
	foreach(const AssetTileData &record, db->fetchAssets()) {
		QJsonObject object;
		object["icon_url"] = "";
        object["guid"] = record.guid;
		object["name"] = record.name;
		object["type"] = record.type;
		object["full_filename"] = record.full_filename;
		object["collection"] = record.collection;
        object["collection_name"] = record.collection_name;

		QImage image;
		image.loadFromData(record.thumbnail, "PNG");

		auto sceneProperties = QJsonDocument::fromBinaryData(record.properties);

		auto gridItem = new AssetGridItem(object, image, sceneProperties.object());

		connect(gridItem, &AssetGridItem::addAssetToProject, [this](AssetGridItem *item) {
			addAssetToProject(item);
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
	QLabel *assetDropPadLabel = new QLabel("Drop a model to import...");
	assetDropPadLayout->setSpacing(4);
	assetDropPadLabel->setObjectName(QStringLiteral("assetDropPadLabel"));
	assetDropPadLabel->setAlignment(Qt::AlignHCenter);

	assetDropPadLayout->addWidget(assetDropPadLabel);
	QPushButton *browseButton = new QPushButton("Browse for file...");
	assetDropPadLayout->addWidget(browseButton);

	addToLibrary = new QPushButton("Add to Library");
	addToLibrary->setStyleSheet("background: #2ecc71");
	addToLibrary->setVisible(false);

    normalize = new QPushButton("Normalize");

	addToProject = new QPushButton("Add to Project");
	addToProject->setStyleSheet("background: #3498db");
	addToProject->setVisible(false);

    deleteFromLibrary = new QPushButton("Remove From Library");
    deleteFromLibrary->setStyleSheet("background: #E74C3C");
    deleteFromLibrary->setVisible(false);

	renameModel = new QLabel("Rename:");
	renameModelField = new QLineEdit();

	renameWidget = new QWidget;
	auto renameLayout = new QHBoxLayout;
	renameLayout->setMargin(0);
	renameLayout->setSpacing(12);
	renameLayout->addWidget(renameModel);
	renameLayout->addWidget(renameModelField);
	renameWidget->setLayout(renameLayout);
	renameWidget->setVisible(false);

    connect(normalize, &QPushButton::pressed, [this]() {

    });

    connect(fastGrid, &AssetViewGrid::selectedTile, [=](AssetGridItem *gridItem) {
		fastGrid->deselectAll();

		if (!gridItem->metadata.isEmpty()) {
			auto material = db->getMaterialGlobal(gridItem->metadata["guid"].toString());
			auto materialObj = QJsonDocument::fromBinaryData(material);

            auto assetPath = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DataLocation),
											 Constants::ASSET_FOLDER,
											 gridItem->metadata["guid"].toString());

			viewer->setMaterial(materialObj.object());

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

			if (viewer->cachedAssets.value(gridItem->metadata["guid"].toString())) {
				viewer->addNodeToScene(viewer->cachedAssets.value(gridItem->metadata["guid"].toString()), QString(), true, false);
				viewer->orientCamera(pos, rot, distObj);
			}
			else {
				viewer->loadModel(QDir(assetPath).filePath(gridItem->metadata["full_filename"].toString()), false, true, !cached);
				viewer->orientCamera(pos, rot, distObj);
			}
           
            split->setHandleWidth(1);
            int size = this->height() / 3;
            const QList<int> sizes = { size, size * 2 };
            split->setSizes(sizes);
            split->setStretchFactor(0, 1);
            split->setStretchFactor(1, 1);

			selectedGridItem = gridItem;
			if (UiManager::isSceneOpen) {
				addToProject->setVisible(true);
				deleteFromLibrary->setVisible(true);
			}
			selectedGridItem->highlight(true);
		}

		fetchMetadata(gridItem);
    });

	connect(addToLibrary, &QPushButton::pressed, [this]() {
		QFileInfo fInfo(filename);
		QJsonObject object;
		object["icon_url"] = "";
		object["name"] = renameModelField->text();

		auto thumbnail = viewer->takeScreenshot(512, 512);

		// add to db
		QByteArray bytes;
		QBuffer buffer(&bytes);
		buffer.open(QIODevice::WriteOnly);
		thumbnail.save(&buffer, "PNG");

		QJsonDocument doc(viewer->getSceneProperties());
		QByteArray sceneProperties = doc.toJson();

		// maybe actually check if Object?
		QString guid = db->insertAssetGlobal(IrisUtils::buildFileName(renameModelField->text(),
											 fInfo.suffix().toLower()),
											 static_cast<int>(ModelTypes::Object), bytes, doc.toBinaryData());
		object["guid"] = guid;
		object["type"] = (int) AssetMetaType::Object; // model?
		object["full_filename"] = IrisUtils::buildFileName(guid, fInfo.suffix());

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

		db->insertMaterialGlobal(QString(), guid, QJsonDocument(viewer->getMaterial()).toBinaryData());

		auto gridItem = new AssetGridItem(object, thumbnail, viewer->getSceneProperties());

		connect(gridItem, &AssetGridItem::addAssetToProject, [this](AssetGridItem *item) {
			addAssetToProject(item);
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


		renameWidget->setVisible(false);
		addToLibrary->setVisible(false);
	});

	connect(addToProject, &QPushButton::pressed, [this]() {
		addAssetToProject(selectedGridItem);
	});

	connect(deleteFromLibrary, &QPushButton::pressed, [this]() {
		removeAssetFromProject(selectedGridItem);
	});

	connect(browseButton, &QPushButton::pressed, [=]() {
		filename = QFileDialog::getOpenFileName(this,
												"Load Mesh",
												QString(),
												"Mesh Files (*.obj *.fbx *.3ds *.dae *.c4d *.blend)");

		// this should switch over to local from online
		importModel(filename);
	});

	assetDropPadLayout->addWidget(renameWidget);
	assetDropPadLayout->addWidget(addToLibrary);
	assetDropPad->setLayout(assetDropPadLayout);

    metaLayout->addWidget(assetDropPad);

	metaLayout->addStretch();
	auto projectSpecific = new QWidget;
	auto ll = new QVBoxLayout;
    // ll->addWidget(normalize);
	ll->addWidget(addToProject);
    ll->addWidget(deleteFromLibrary);
	projectSpecific->setLayout(ll);
	metaLayout->addWidget(projectSpecific);
	auto metadata = new QWidget;
	//metadata->setFixedHeight(256);
	auto l = new QVBoxLayout;
	l->setSpacing(12);
	//l->setMargin(0);
	QSizePolicy policy2;
	policy2.setVerticalPolicy(QSizePolicy::Preferred);
	policy2.setHorizontalPolicy(QSizePolicy::Preferred);
	metadataMissing = new QLabel("Nothing selected...");
	metadataMissing->setStyleSheet("padding: 12px");
	metadataMissing->setSizePolicy(policy2);
	metadataName = new QLabel("Name: ");
	metadataName->setSizePolicy(policy2);
	metadataType = new QLabel("Type: ");
	metadataType->setSizePolicy(policy2);
	metadataVisibility = new QLabel("Public: ");
	metadataVisibility->setSizePolicy(policy2);
	metadataCollection = new QLabel("Collection: ");
	metadataCollection->setSizePolicy(policy2);
    metadataVisibility->setVisible(false);

	metadataName->setVisible(false);
	metadataType->setVisible(false);

    changeMetaCollection = new QPushButton(tr("change"));
    changeMetaCollection->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    changeMetaCollection->setStyleSheet("font-size: 8px; color: green; padding: 0; background: transparent; border: 0");
    metadataLayout = new QHBoxLayout;
    metadataLayout->setMargin(0);
    metadataLayout->setSpacing(12);
    metadataLayout->addWidget(metadataCollection);
    metadataLayout->addWidget(changeMetaCollection);
    metadataLayout->addStretch();
    metadataWidget = new QWidget;
    metadataWidget->setLayout(metadataLayout);
	metadataWidget->setVisible(false);

	l->addWidget(metadataMissing);
	l->addWidget(metadataName);
	l->addWidget(metadataType);
	l->addWidget(metadataVisibility);
	l->addWidget(metadataWidget);
	metadata->setLayout(l);
	metadata->setStyleSheet("QLabel { font-size: 12px; font-weight: bold; }");
	auto header = new QLabel("Asset Metadata");
	header->setAlignment(Qt::AlignCenter);
	header->setStyleSheet("font-size: 14px; font-weight: bold; border-top: 1px solid black; border-bottom: 1px solid black; text-align: center; padding: 12px; background: #1e1e1e");
	metaLayout->addWidget(header);
	metaLayout->addWidget(metadata);
    _metadataPane->setLayout(metaLayout);

	split->addWidget(viewer);
	split->addWidget(_viewPane);

    closeViewer();

	split->setHandleWidth(0);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);

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
		"QPushButton				{ border-radius: 2px; }"
		"QSplitter					{ background: #2E2E2E; } QSplitter:handle { background: black; }"
		"#localAssetsButton			{ font-size: 12px; font-weight: bold; text-align: left; padding: 12px; }"
		"#onlineAssetsButton		{ font-size: 12px; font-weight: bold; text-align: left; padding: 12px; }"
        "QPushButton[accessibleName=\"assetsButton\"]:disabled { color: #444; }"
		"#treeWidget				{ font-size: 12px; font-weight: bold; background: transparent; }"
		"#assetDropPad				{}"
		"#assetDropPadLabel			{ font-size: 14px; font-weight: bold; border: 4px dashed #1E1E1E; border-radius: 4px; "
		"							  padding: 48px 36px; }"
		"#assetDropPad, #MetadataPane QPushButton	{ font-size: 12px; font-weight: bold; padding: 8px; }"
		"#assetDropPad QLineEdit	{ border: 1px solid #1E1E1E; border-radius: 2px;"
		"							  font-size: 12px; font-weight: bold; background: #3B3B3B; padding: 6px 4px; }"
		"#assetDropPad QLabel		{ font-size: 12px; font-weight: bold; }"
	);
}

void AssetView::importModel(const QString &filename)
{
	if (!filename.isEmpty()) {
		renameModelField->setText(QFileInfo(filename).baseName());

		renameWidget->setVisible(true);
		addToLibrary->setVisible(true);
		viewer->loadModel(filename);

		split->setHandleWidth(1);
		int size = this->height() / 3;
		const QList<int> sizes = { size, size * 2 };
		split->setSizes(sizes);
		split->setStretchFactor(0, 1);
		split->setStretchFactor(1, 1);
	}
}

void AssetView::fetchMetadata(AssetGridItem *widget)
{
	if (!widget->metadata.isEmpty()) {
		metadataMissing->setVisible(false);

		metadataName->setVisible(true);
		metadataType->setVisible(true);
		metadataVisibility->setVisible(true);
        metadataWidget->setVisible(true);

		metadataName->setText("Name: " + widget->metadata["name"].toString());
		metadataType->setText("Type: " + getAssetType(widget->metadata["type"].toInt()));
		QString pub = widget->metadata["is_public"].toBool() ? "true" : "false";
		metadataVisibility->setText("Public: " + pub);
		metadataCollection->setText("Collection: " + widget->metadata["collection_name"].toString());
	}
	else {
		metadataMissing->setVisible(true);

		addToProject->setVisible(false);
        deleteFromLibrary->setVisible(false);

		metadataName->setVisible(false);
		metadataType->setVisible(false);
		metadataVisibility->setVisible(false);
        metadataWidget->setVisible(false);
	}
}

void AssetView::addAssetToProject(AssetGridItem *item)
{
	//addToProject->setVisible(false);
	// get the current project working directory
	auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
		Constants::PROJECT_FOLDER);
	auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();
	auto pDir = IrisUtils::join(defaultProjectDirectory, Globals::project->getProjectName());

	auto guid = item->metadata["guid"].toString();
	int assetType = item->metadata["type"].toInt();
	QFileInfo fInfo(item->metadata["name"].toString());
	QString object = IrisUtils::buildFileName(guid, fInfo.suffix());
	auto assetPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + Constants::ASSET_FOLDER;

	QString assetFolder;

	if (assetType == (int) AssetMetaType::Object) {
		assetFolder = "Models";
	}
	else {
		assetFolder = QString();
	}

	// todo -- undo progress?
	auto copyFolder = [](const QString &src, const QString &dest) {
		if (!QDir(dest).exists()) {
			if (!QDir().mkdir(dest)) return false;

			for (auto file : QDir(src).entryInfoList(QStringList(), QDir::Files)) {
				if (!QFile::copy(file.absoluteFilePath(), QDir(dest).filePath(file.fileName()))) return false;
			}

			return true;
		}
	};

	if (!copyFolder(QDir(assetPath).filePath(guid), QDir(QDir(pDir).filePath(assetFolder)).filePath(guid))) {
		QString warningText = QString("Failed to import asset %1. Possible reasons are:\n"
			"1. It doesn't exist\n"
			"2. The asset isn't valid")
			.arg(item->metadata["name"].toString());
		QMessageBox::warning(this, "Asset Import Failed", warningText, QMessageBox::Ok);
	}
	else {
		QString warningText = QString("Added asset %1 to your project!")
			.arg(item->metadata["name"].toString());
		QMessageBox::information(this, "Asset Import Successful", warningText, QMessageBox::Ok);
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
		"QPushButton:hover { background: #51a1d6; }"
		"QDialog { background: #1a1a1a; }");

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
	        db->deleteAsset(item->metadata["guid"].toString());
	        closeViewer();
	    }
	    else {
	        QMessageBox::warning(this, "Delete Failed!", "Failed to remove asset, please try again!", QMessageBox::Ok);
	    }
	}
}

AssetView::~AssetView()
{
    
}