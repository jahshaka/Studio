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

#include <QtAlgorithms>
#include <QFile>

#include <QTreeWidget>
#include <QHeaderView>
#include <QTreeWidgetItem>

#include "assetviewgrid.h"
#include "assetgriditem.h"
#include "assetviewer.h"

void AssetView::focusInEvent(QFocusEvent *event)
{
	Q_UNUSED(event);
	//emit fetch();
}

bool AssetView::eventFilter(QObject * watched, QEvent * event)
{
	if (watched == fastGrid) {
		/*switch (event->type()) {
		
		}*/
	}

	return QObject::eventFilter(watched, event);
}

AssetView::AssetView(QWidget *parent) : QWidget(parent)
{
	_assetView = new QListWidget;

	viewer = new AssetViewer(this);

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
	fastGrid->installEventFilter(this);

    // gui
    _splitter = new QSplitter(this);
	_splitter->setHandleWidth(1);

    //QWidget *_filterBar;
    _navPane = new QWidget; 
    QVBoxLayout *navLayout = new QVBoxLayout;
	navLayout->setSpacing(6);
    _navPane->setLayout(navLayout);

	QTreeWidget *treeWidget = new QTreeWidget;
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

	//QTreeWidgetItem *treeItem = new QTreeWidgetItem;
	//treeItem->setText(0, "Node");
	//treeItem->setIcon(1, QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/world.svg")));

	//QTreeWidgetItem *treeItem2 = new QTreeWidgetItem;
	//treeItem2->setText(0, "Node2");
	//treeItem2->setIcon(1, QIcon(IrisUtils::getAbsoluteAssetPath("app/icons/world.svg")));

	//rootItem->addChild(treeItem);
	//rootItem->addChild(treeItem2);
	treeWidget->addTopLevelItem(rootItem);
	//treeWidget->expandItem(rootItem);

    navLayout->addWidget(localAssetsButton);
	navLayout->addWidget(onlineAssetsButton);
	navLayout->addSpacing(12);
	navLayout->addWidget(treeWidget);
	auto collectionButton = new QPushButton("Create Collection");
	collectionButton->setStyleSheet("font-size: 12px; font-weight: bold; padding: 8px;");
	navLayout->addWidget(collectionButton);

    //QWidget *_previewPane;  
	auto split = new QSplitter;
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

	auto typeObject = new QPushButton();
	typeObject->setAccessibleName("filterObj");
	typeObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-purchase-order-50.png")));
	typeObject->setIconSize(QSize(16, 16));

	auto scriptObject = new QPushButton();
	scriptObject->setAccessibleName("filterObj");
	scriptObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-music-50.png")));
	scriptObject->setIconSize(QSize(16, 16));
	scriptObject->setStyleSheet("border-top-right-radius: 2px; border-bottom-right-radius: 2px;");

	auto imageObject = new QPushButton();
	imageObject->setAccessibleName("filterObj");
	imageObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-picture-50.png")));
	imageObject->setIconSize(QSize(16, 16));

	auto meshObject = new QPushButton();
	meshObject->setAccessibleName("filterObj");
	meshObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-cube-filled-50.png")));
	meshObject->setIconSize(QSize(16, 16));
	meshObject->setStyleSheet("border-top-left-radius: 2px; border-bottom-left-radius: 2px;");

	QWidget *filterGroup = new QWidget;
	auto fgL = new QHBoxLayout;
	fgL->addWidget(meshObject);
	fgL->addWidget(typeObject);
	fgL->addWidget(imageObject);
	fgL->addWidget(scriptObject);
	filterGroup->setLayout(fgL);
	fgL->setMargin(0);
	fgL->setSpacing(0);

	filterPane = new QWidget;
	auto filterLayout = new QHBoxLayout;
	filterLayout->addWidget(new QLabel("Filter: "));
	filterLayout->addWidget(filterGroup);
	filterLayout->addStretch();
	filterLayout->addWidget(new QLabel("Search: "));
	auto le = new QLineEdit();
	le->setFixedWidth(256);
	le->setStyleSheet(
		"border: 1px solid #1E1E1E; border - radius: 2px; "
		"font-size: 12px; font-weight: bold; background: #3B3B3B; padding: 6px 4px;"
	);
	filterLayout->addWidget(le);
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
		}
	});

    _metadataPane = new QWidget; 
    QVBoxLayout *metaLayout = new QVBoxLayout;
	metaLayout->setMargin(0);
	auto assetDropPad = new QWidget;
	assetDropPad->setAcceptDrops(true);
	assetDropPad->installEventFilter(this);
	QSizePolicy policy;
	policy.setHorizontalPolicy(QSizePolicy::Expanding);
	assetDropPad->setSizePolicy(policy);
	auto assetDetails = new QWidget;
	assetDropPad->setObjectName(QStringLiteral("assetDropPad"));
	auto assetDropPadLayout = new QVBoxLayout;
	QLabel *assetDropPadLabel = new QLabel("Drop model to import...");
	assetDropPadLabel->setObjectName(QStringLiteral("assetDropPadLabel"));
	assetDropPadLabel->setAlignment(Qt::AlignHCenter);
	assetDropPadLayout->addWidget(assetDropPadLabel);
	assetDropPadLayout->addSpacing(4);
	QPushButton *browseButton = new QPushButton("Browse for file...");
	assetDropPadLayout->addWidget(browseButton);
	// temp
	nameField = new QLineEdit;
	nameField->setVisible(false);
	typeField = new QComboBox;
	typeField->setVisible(false);
	uploadBtn = new QPushButton("Upload");
	uploadBtn->setVisible(false);

	addToLibrary = new QPushButton("Add to Library");
	addToLibrary->setStyleSheet("background: #2ecc71");
	addToLibrary->setVisible(false);

	addToProject = new QPushButton("Add to Project");
	addToProject->setStyleSheet("background: #3498db");
	addToProject->setVisible(false);

	renameModel = new QLabel("Rename");
	renameModelField = new QLineEdit();

	renameWidget = new QWidget;
	auto renameLayout = new QHBoxLayout;
	renameLayout->setMargin(0);
	renameLayout->setSpacing(12);
	renameLayout->addWidget(renameModel);
	renameLayout->addWidget(renameModelField);
	renameWidget->setLayout(renameLayout);
	renameWidget->setVisible(false);

	connect(addToLibrary, &QPushButton::pressed, [this]() {
		QJsonObject object;
		object["icon_url"] = "";
		object["name"] = renameModelField->text();

		// render thumb tho
		fastGrid->addTo(object, viewer->takeScreenshot(512, 512), 0);
		QApplication::processEvents();
		fastGrid->updateGridColumns(fastGrid->lastWidth);

		renameWidget->setVisible(false);
		addToLibrary->setVisible(false);
		addToProject->setVisible(true);
	});

	connect(addToProject, &QPushButton::pressed, [this]() {
		addToProject->setVisible(false);
	});

	connect(browseButton, &QPushButton::pressed, [=]() {
		filename = QFileDialog::getOpenFileName(this,
												"Load Mesh",
												QString(),
												"Mesh Files (*.obj *.fbx *.3ds *.dae *.c4d *.blend)");

		// this should switch over to local from online
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

			if (assetSource == AssetSource::ONLINE) {
				nameField->setVisible(true);
				typeField->setVisible(true);
				uploadBtn->setVisible(true);
			}
		}
	});

	assetDropPadLayout->addSpacing(4);
	assetDropPadLayout->addWidget(renameWidget);
	assetDropPadLayout->addSpacing(4);
	assetDropPadLayout->addWidget(addToLibrary);
	assetDropPadLayout->addWidget(nameField);
	assetDropPadLayout->addWidget(typeField);
	assetDropPadLayout->addWidget(uploadBtn);
	assetDropPadLayout->addStretch();
	assetDropPadLayout->addWidget(addToProject);
	assetDropPad->setLayout(assetDropPadLayout);
    metaLayout->addWidget(assetDropPad);
	metaLayout->addWidget(assetDetails);

	metaLayout->addStretch();
	auto metadata = new QWidget;
	//metadata->setFixedHeight(256);
	auto l = new QVBoxLayout;
	l->setSpacing(12);
	//l->setMargin(0);
	QSizePolicy policy2;
	policy2.setVerticalPolicy(QSizePolicy::Preferred);
	policy2.setHorizontalPolicy(QSizePolicy::Preferred);
	metadataName = new QLabel("Name: ");
	metadataName->setSizePolicy(policy2);
	metadataType = new QLabel("Type: ");
	metadataType->setSizePolicy(policy2);
	metadataVisibility = new QLabel("Public: ");
	metadataVisibility->setSizePolicy(policy2);
	metadataCollection = new QLabel("Collection: ");
	metadataCollection->setSizePolicy(policy2);
	l->addWidget(metadataName);
	l->addWidget(metadataType);
	l->addWidget(metadataVisibility);
	l->addWidget(metadataCollection);
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
		"#assetDropPad QPushButton	{ font-size: 12px; font-weight: bold; padding: 8px; }"
		"#assetDropPad QLineEdit	{ border: 1px solid #1E1E1E; border-radius: 2px;"
		"							  font-size: 12px; font-weight: bold; background: #3B3B3B; padding: 6px 4px; }"
		"#assetDropPad QLabel		{ font-size: 12px; font-weight: bold; }"
	);
}

void AssetView::fetchMetadata(AssetGridItem *widget)
{
	metadataName->setText("Name: " + widget->metadata["name"].toString());
	metadataType->setText("Type: " + QString::number(widget->metadata["type"].toInt()));
	QString pub = widget->metadata["is_public"].toBool() ? "true" : "false";
	metadataVisibility->setText("Public: " + pub);
	metadataCollection->setText("Collection: " + QString::number(widget->metadata["collection_id"].toInt()));
}

AssetView::~AssetView()
{
    
}