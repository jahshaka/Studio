
#include "minerui.h"
#include <QComboBox>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>

MinerUI::MinerUI(QWidget *parent)
	: QWidget(parent)
{
	configureUI();
	configureSettings();
	configureConnections();
	configureStyleSheet();
	setAttribute(Qt::WA_TranslucentBackground);
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
	// setWindowFlag(Qt::SubWindow);
	setAttribute(Qt::WA_QuitOnClose, false);
	setWindowModality(Qt::ApplicationModal);
}

MinerUI::~MinerUI()
{

}

void MinerUI::addGraphicsCard(QString string)
{
	GraphicsCardUI *card = new GraphicsCardUI();
	card->setCardName(string);
	card->setObjectName(QStringLiteral("card"));
	list.append(card);
	cardHolderLayout->addWidget(card);
}

void MinerUI::configureUI()
{
	setContentsMargins(20, 20, 20, 20);

	QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
	effect->setBlurRadius(20);
	effect->setXOffset(0);
	effect->setYOffset(0);
	effect->setColor(QColor(0, 0, 0, 200));
	//setGraphicsEffect(effect);


	stack = new QStackedWidget;
	stack->setGraphicsEffect(effect);


	// fontIcon.initFontAwesome();

	auto groupBoxLayout = new QVBoxLayout;
	auto mainLayout = new QVBoxLayout;
	auto autoLayout = new QVBoxLayout;
	auto bottomLayout = new QHBoxLayout;
	cardHolderLayout = new QVBoxLayout;

	setLayout(mainLayout);
	mainLayout->addWidget(stack);

	auto groupBox = new QGroupBox;
	groupBox->setLayout(groupBoxLayout);
	groupBox->setFixedSize(500, 300);
	//mainLayout->addWidget(groupBox);
	stack->addWidget(groupBox);

	//configure toolbar


	toolbar = new QToolBar();
	toolbar->setObjectName(QStringLiteral("toolBar"));
	settings = new QAction("settings");
	//settings->setText(QChar(fa::cog));
	//settings->setFont(fontIcon.font(15));
	advance = new QAction("advance");
	//advance->setText(QChar(fa::sliders));
	//advance->setFont(fontIcon.font(15));
	close = new QAction("X");
	//close->setText(QChar(fa::times));
	//close->setFont(fontIcon.font(15));

	toolbar->addAction(settings);
	toolbar->addAction(advance);
	auto empty = new QWidget();
	empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolbar->addWidget(empty);
	toolbar->addAction(close);


	scrollArea = new QScrollArea(this);
	scrollArea->setContentsMargins(0, 3, 3, 3);
	scrollArea->setAlignment(Qt::AlignTop);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);


	cardHolder = new QWidget();
	cardHolder->setObjectName(QStringLiteral("cardHolder"));
	cardHolder->setGeometry(0, 0, 450, 400);

	for (int i = 0; i< 2; i++) {
		addGraphicsCard("amd numbering at this pos " + QString::number(i));
	}

	cardHolder->setLayout(cardHolderLayout);
	cardHolderLayout->setSizeConstraint(QLayout::SetFixedSize);
	cardHolderLayout->setContentsMargins(0, 0, 0, 0);
	cardHolderLayout->setSpacing(2);
	scrollArea->setWidget(cardHolder);

	startBtn = new QPushButton("Start", this);
	coinType = new QLabel("JFX: 0.00123");
	autostart = new QLabel("auto start");
	autostart->setAlignment(Qt::AlignHCenter);
	startBtn->setObjectName(QStringLiteral("startBtn"));

	QFont font = coinType->font();
	font.setStyleStrategy(QFont::PreferAntialias);
	font.setBold(true);
	coinType->setFont(font);
	//autostart->setFont(font);


	autoStartSwitch = new MSwitch();
	autoStartSwitch->setColor(QColor(40, 128, 185));
	autoStartSwitch->setSizeOfSwitch(20);
	auto switchLayout = new QHBoxLayout;
	//switchLayout->addStretch();
	switchLayout->addWidget(autoStartSwitch);
	//switchLayout->addStretch();


	autoLayout->addLayout(switchLayout);
	autoLayout->addWidget(autostart);

	bottomLayout->addWidget(startBtn);
	bottomLayout->addSpacing(23);
	bottomLayout->addWidget(coinType);
	bottomLayout->addStretch();
	bottomLayout->addLayout(autoLayout);


	groupBoxLayout->addWidget(toolbar);
	groupBoxLayout->addSpacing(3);
	groupBoxLayout->addWidget(scrollArea);
	groupBoxLayout->addSpacing(3);
	groupBoxLayout->addLayout(bottomLayout);
	//  groupBoxLayout->setSizeConstraint(QLayout::SetFixedSize);
	groupBoxLayout->setSpacing(0);
}

void MinerUI::configureSettings()
{
	auto settingsWidget = new QWidget;
	auto settingsLaout = new QGridLayout;
	settingsLaout->setContentsMargins(5, 0, 5, 10);
	settingsWidget->setLayout(settingsLaout);
	settingsWidget->setObjectName(QStringLiteral("settingsWidget"));

	auto toolbar = new QWidget;
	toolbar->setObjectName(QStringLiteral("toolBar"));
	toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	auto toolbarLayout = new QHBoxLayout;
	toolbar->setLayout(toolbarLayout);

	auto vHolder = new QWidget;
	auto vLayout = new QVBoxLayout;
	vHolder->setLayout(vLayout);
	vHolder->setFixedSize(400, 200);
	vLayout->addWidget(settingsWidget);
	vLayout->setContentsMargins(10, 0, 10, 20);


	back = new QPushButton("<-- back");
	back->setObjectName(QStringLiteral("back"));
	auto settingsLabel = new QLabel("SETTINGS");
	settingsLabel->setAlignment(Qt::AlignHCenter);
	QFont font = settingsLabel->font();
	font.setBold(true);
	settingsLabel->setFont(font);

	auto settingsClose = new QPushButton("close");
	settingsClose->setObjectName(QStringLiteral("back"));
	connect(settingsClose, SIGNAL(clicked(bool)), close, SLOT(trigger()));

	auto confirm = new QPushButton("Confirm");
	auto CancelBtn = new QPushButton("Cancel");
	confirm->setObjectName(QStringLiteral("bottomBtn"));
	CancelBtn->setObjectName(QStringLiteral("bottomBtn"));

	connect(confirm, &QPushButton::clicked, [=]() {
		back->animateClick();
	});
	connect(CancelBtn, &QPushButton::clicked, [=]() {
		back->animateClick();
	});



	toolbarLayout->addWidget(back);
	toolbarLayout->addStretch();
	toolbarLayout->addWidget(settingsLabel);
	toolbarLayout->addStretch();
	toolbarLayout->addWidget(settingsClose);



	auto walletLabel = new QLabel("Wallet Id :");
	auto password = new QLabel("Password :");
	auto poolUrl = new QLabel("Pool URL :");
	auto identifier = new QLabel("Identifier :");
	auto currencyLabel = new QLabel("currency");
	walletLabel->setObjectName(QStringLiteral("label"));
	password->setObjectName(QStringLiteral("label"));
	poolUrl->setObjectName(QStringLiteral("label"));
	identifier->setObjectName(QStringLiteral("label"));
	currencyLabel->setObjectName(QStringLiteral("label"));
	walletLabel->setFont(font);
	password->setFont(font);
	poolUrl->setFont(font);
	identifier->setFont(font);
	currencyLabel->setFont(font);



	currency = new QComboBox;
	auto stringList = QStringList() << "Aeon" << "BBSCoin" << "Croat" << "Edollar" << "Electroneum" << "Graft" << "Haven" << "Intense" << "Karbo" << "Sumokoin" << "Monero7";
	currency->setCurrentText("Select Currency");
	currency->addItems(stringList);

	settingsLaout->addWidget(toolbar, 0, 0, 1, 2);
	// settingsLaout->add
	settingsLaout->addWidget(walletLabel, 2, 0);
	settingsLaout->addWidget(password, 3, 0);
	settingsLaout->addWidget(poolUrl, 4, 0);
	settingsLaout->addWidget(identifier, 5, 0);
	settingsLaout->addWidget(currencyLabel, 6, 0);
	settingsLaout->addWidget(currency, 6, 1);

	settingsLaout->addWidget(confirm, 8, 0);
	settingsLaout->addWidget(CancelBtn, 8, 1);

	// settingsLaout->setSizeConstraint(QLayout::SetFixedSize);

	stack->addWidget(vHolder);



}

void MinerUI::configureConnections()
{
	connect(settings, &QAction::triggered, [this]() {
		stack->setCurrentIndex(1);
	});

	connect(back, &QPushButton::clicked, [this]() {
		stack->setCurrentIndex(0);

	});

	connect(currency, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {

	});

	connect(advance, SIGNAL(triggered(bool)), this, SLOT(switchToAdvanceMode()));
	connect(close, &QAction::triggered, [this]() {
		hide();
	});

	connect(startBtn, &QPushButton::clicked, [this]() {
		foreach(card, list) card->setStarted();
		startBtn->setText("Stop");

	});
	connect(autoStartSwitch, &MSwitch::switchPressed, [this](bool val) {
		startAutomatically = val;
	});

	foreach(card, list) {
		connect(card, &GraphicsCardUI::switchIsOn, [this](bool val) {
			//do something;
		});
	}
}

void MinerUI::configureStyleSheet()
{
	setStyleSheet("QGroupBox, #settingsWidget{ background: rgba(33,33,33,1); margin:0px; padding : 0px; border: 0px solid black; }"
		"QScrollArea, #cardHolder{ border: 0px solid rgba(130,130,130,0); background: rgba(17,17,17,0); border-radius:1px; }"
		"#cardHolder{background: rgba(17,17,17,0); padding: 0px; margin: 0px; }"
		"QLabel{ color: rgba(255,255,255,.9); }"
		"QLabel#label{ padding-left: 10px; background:rgba(10,10,10,0); }"
		"QToolButton {border-radius: 1px; background: rgba(20,20,20, 0); color: rgba(250,250,250, 1); border : 0px solid rgba(20,20,20, 1); padding: 4px 6px 4px 6px ; margin-right:3px;}"
		//     "QToolButton:hover{background: rgba(48,48,48, 1);}"
		"QScrollBar::handle {background: rgba(40,128, 185,.9); border-radius: 4px; right: 1px; width: 8px;}"
		"QScrollBar{border : 0px solid black; background-color: rgba(32,32,32,.1); width: 8px;padding: 1px;}"
		" QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {background: rgba(200,200,200,0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical { background: rgba(0,0,0,0); border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {background: rgba(10,0,0,.1);}"
		"QPushButton{ background: rgba(20,20,20,1); border: 1px solid rgba(10,10,10,1); border-radius: 1px;  color: rgba(255,255,255,.9); padding : 3px 9px 3px 9px; }"
		""
		"#startBtn{ padding: 9px 19px 9px 19px; background:rgba(23,23,23,.7); border:1px solid rgba(0,0,0,0);}"
		"#startBtn:hover, QToolButton:hover, #back:hover { background : rgba(40,128, 185,.9); }"
		//   "QScrollArea{background: rgba(23,23,23,1); border: 0px solid black; }"
		"#toolBar{ background: rgba(40,128, 185,0); border: 1px solid rgba(10,0,0,0); }"
		"#back{ background: rgba(40,128, 185,0); border: 0px solid rgba(40,40,40,0.3); }"
		"#bottomBtn{border: 1px solid rgba(40,40,40,0.3); }"
		"#bottomBtn:hover{background: rgba(40,128, 185,0.5);};"
		"");
}

void MinerUI::switchToAdvanceMode()
{
	if (!isInAdvanceMode) {
		foreach(card, list)   card->expand();
		isInAdvanceMode = true;
	}
	else {
		foreach(card, list)   card->contract();
		isInAdvanceMode = false;
	}


}

