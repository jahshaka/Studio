
#include "minerui.h"
#include <QComboBox>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QSizeGrip>
#include <QStyledItemDelegate>
#include <QStandardPaths>
#include "minerprocess.h"
#include "../core/settingsmanager.h"

MinerUI::MinerUI(QWidget *parent)
	: QWidget(parent)
{
	minerMan = new MinerManager();
	minerMan->initialize();

	settingsMan = new SettingsManager("jahminer.ini");

	configureUI();
	configureSettings();
	configureConnections();
	configureStyleSheet();
	setAttribute(Qt::WA_TranslucentBackground);
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
	// setWindowFlag(Qt::SubWindow);
	setAttribute(Qt::WA_QuitOnClose, false);
	setWindowModality(Qt::ApplicationModal);

	// add cards
	/*
	for (auto process : minerMan->processes) {
		auto card = this->addGraphicsCard(process->gpu.name);
		card->setMinerProcess(process);
		card->startMining();
	}
	*/
}

MinerUI::~MinerUI()
{

}

GraphicsCardUI* MinerUI::addGraphicsCard(QString string)
{
	GraphicsCardUI *card = new GraphicsCardUI();
	card->setCardName(string);
	card->setObjectName(QStringLiteral("card"));
	list.append(card);
	cardHolderLayout->addWidget(card);
	return card;
}

void MinerUI::configureUI()
{
	setContentsMargins(20, 20, 20, 20);

	QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
	effect->setBlurRadius(20);
	effect->setXOffset(0);
	effect->setYOffset(0);
	effect->setColor(QColor(0, 0, 0, 200));
	setGraphicsEffect(effect);


	stack = new QStackedWidget;
	//stack->setGraphicsEffect(effect);


	 fontIcon.initFontAwesome();

	auto groupBoxLayout = new QVBoxLayout;
	auto mainLayout = new QVBoxLayout;
	auto autoLayout = new QVBoxLayout;
	auto bottomLayout = new QHBoxLayout;
	cardHolderLayout = new QVBoxLayout;

	setLayout(mainLayout);
	mainLayout->addWidget(stack);

	auto groupBox = new QGroupBox;
	groupBox->setLayout(groupBoxLayout);
	//groupBox->setFixedSize(500, 300);
	//mainLayout->addWidget(groupBox);
	stack->addWidget(groupBox);

	//configure toolbar


	toolbar = new QToolBar();
	toolbar->setObjectName(QStringLiteral("toolBar"));
	settings = new QAction("settings");
	settings->setText(QChar(fa::cog));
	settings->setFont(fontIcon.font(15));
	advance = new QAction("advance");
	advance->setText(QChar(fa::sliders));
	advance->setFont(fontIcon.font(15));
	close = new QAction("X");
	close->setText(QChar(fa::times));
	close->setFont(fontIcon.font(15));

	auto minerLabel = new QLabel("Miner");
	
	toolbar->addAction(settings);
	toolbar->addAction(advance);
	auto empty = new QWidget();
	empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolbar->addWidget(empty);
	toolbar->addWidget(minerLabel);
	auto empty1 = new QWidget();
	empty1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolbar->addWidget(empty1);
	toolbar->addAction(close);


	scrollArea = new QScrollArea(this);
	scrollArea->setContentsMargins(0, 3, 3, 3);
	scrollArea->setAlignment(Qt::AlignTop);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setWidgetResizable(true);


	cardHolder = new QWidget();
	cardHolder->setObjectName(QStringLiteral("cardHolder"));
	cardHolder->setGeometry(0, 0, 450, 400);
	cardHolder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	for (int i = 0; i< 2; i++) {
		//addGraphicsCard("amd numbering at this pos " + QString::number(i));
	}
	// todo: move back to top (nick)
	for (auto process : minerMan->processes) {
		auto card = this->addGraphicsCard(process->gpu.name);
		card->setMinerProcess(process);
		card->startMining();
	}

	cardHolder->setLayout(cardHolderLayout);
	cardHolderLayout->addStretch();
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
	minerLabel->setFont(font);
	//autostart->setFont(font);


	autoStartSwitch = new MSwitch();
	autoStartSwitch->setColor(QColor(40, 128, 185));
	autoStartSwitch->setSizeOfSwitch(20);
	autoStartSwitch->setChecked(settingsMan->getValue("miner_auto_start", false).toBool());
	auto switchLayout = new QHBoxLayout;
	//switchLayout->addStretch();
	switchLayout->addWidget(autoStartSwitch);
	//switchLayout->addStretch();

	autoLayout->addStretch();
	autoLayout->addLayout(switchLayout);
	autoLayout->addWidget(autostart);
	autoLayout->addStretch();

	bottomLayout->addSpacing(10);
	bottomLayout->addWidget(startBtn);
	bottomLayout->addSpacing(23);
	bottomLayout->addWidget(coinType);
	bottomLayout->addStretch();
	bottomLayout->addLayout(autoLayout);
	bottomLayout->addSpacing(10);

	auto bottomWidget = new QWidget;
	bottomWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	bottomWidget->setLayout(bottomLayout);

	groupBoxLayout->addWidget(toolbar);
	groupBoxLayout->addSpacing(3);
	groupBoxLayout->addWidget(scrollArea);
	groupBoxLayout->addSpacing(3);
	//groupBoxLayout->addLayout(bottomLayout);
	groupBoxLayout->addWidget(bottomWidget);
	//  groupBoxLayout->setSizeConstraint(QLayout::SetFixedSize);
	groupBoxLayout->setSpacing(0);
	groupBoxLayout->addWidget(new QSizeGrip(this), 0, Qt::AlignBottom | Qt::AlignRight);

}

void MinerUI::configureSettings()
{
	auto settingsWidget = new QWidget;
	auto settingsLaout = new QVBoxLayout;
	//settingsLaout->setContentsMargins(10, 6, 10,4 );
	settingsLaout->setSpacing(10);
	settingsWidget->setLayout(settingsLaout);
	settingsWidget->setObjectName(QStringLiteral("settingsWidget"));

	auto toolbar = new QToolBar;
	toolbar->setObjectName(QStringLiteral("toolBar"));

	back = new QAction;
	back->setText(QChar(fa::arrowleft));
	back->setFont(fontIcon.font(15));
	back->setObjectName(QStringLiteral("back"));
	auto settingsLabel = new QLabel("SETTINGS");
//	settingsLabel->setAlignment(Qt::AlignHCenter);
	QFont font = settingsLabel->font();
	font.setBold(true);
	settingsLabel->setFont(font);

	auto settingsClose = new QAction;
	settingsClose->setText(QChar(fa::times));
	settingsClose->setFont(fontIcon.font(15));
	settingsClose->setObjectName(QStringLiteral("back"));

	connect(settingsClose, SIGNAL(triggered()), close, SLOT(trigger()));

	auto confirm = new QPushButton("Confirm");
	auto CancelBtn = new QPushButton("Cancel");
	confirm->setObjectName(QStringLiteral("bottomBtn"));
	CancelBtn->setObjectName(QStringLiteral("bottomBtn"));

	connect(confirm, &QPushButton::clicked, [=]() {
		back->trigger();
		this->saveAndApplySettings();
	});
	connect(CancelBtn, &QPushButton::clicked, [=]() {
		back->trigger();
		this->restoreSettings();
	});


	toolbar->addAction(back);
	auto empty = new QWidget();
	empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolbar->addWidget(empty);	
	toolbar->addWidget(settingsLabel);
	auto empty1 = new QWidget();
	empty1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	toolbar->addWidget(empty1);
	toolbar->addAction(settingsClose);


	auto walletLabel = new QLabel("Wallet Id ");
	auto password = new QLabel("Password ");
	auto poolUrl = new QLabel("Pool URL ");
	auto identifier = new QLabel("Identifier ");
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

	walletEdit = new QLineEdit();
	passwordEdit = new QLineEdit();
	poolEdit = new QLineEdit();
	identifierEdit = new QLineEdit();

	QStyledItemDelegate *itemDelegate = new QStyledItemDelegate();
	currency = new QComboBox;
	currency->setItemDelegate(itemDelegate);
	currency->setObjectName(QStringLiteral("currencyBox"));
	auto stringList = QStringList() << "Monero7" << "Aeon" << "BBSCoin" << "Croat" << "Edollar" << "Electroneum" << "Graft" << "Haven" << "Intense" << "Karbo" << "Sumokoin" ;
	currency->setCurrentText("Select Currency");
	currency->addItems(stringList);

	auto walletLayout = new QVBoxLayout;
	auto poolLayout = new QVBoxLayout;
	auto passwordLayout = new QVBoxLayout;
	auto identifierLayout = new QVBoxLayout;
	auto currencyLayout = new QHBoxLayout;
	auto buttonLayout = new QHBoxLayout;

	walletEdit->setObjectName(QStringLiteral("edit"));
	poolEdit->setObjectName(QStringLiteral("edit"));
	passwordEdit->setObjectName(QStringLiteral("edit"));
	identifierEdit->setObjectName(QStringLiteral("edit"));

	walletEdit->setPlaceholderText("Enter Wallet ID");
	poolEdit->setPlaceholderText("Enter Pool URL");
	passwordEdit->setPlaceholderText("Enter Password");
	identifierEdit->setPlaceholderText("Enter Identifier");	 

	walletLayout->addWidget(walletLabel);
	walletLayout->addWidget(walletEdit);
	poolLayout->addWidget(poolUrl);
	poolLayout->addWidget(poolEdit);
	passwordLayout->addWidget(password);
	passwordLayout->addWidget(passwordEdit);
	identifierLayout->addWidget(identifier);
	identifierLayout->addWidget(identifierEdit);
	buttonLayout->addWidget(confirm);
	buttonLayout->addWidget(CancelBtn);
	currencyLayout->addWidget(currencyLabel);
	currencyLayout->addWidget(currency);

	settingsLaout->addWidget(toolbar);
	settingsLaout->addStretch();
	settingsLaout->addLayout(walletLayout);
	settingsLaout->addLayout(poolLayout);
	settingsLaout->addLayout(passwordLayout);
	settingsLaout->addLayout(identifierLayout);
	settingsLaout->addLayout(currencyLayout);
	settingsLaout->addStretch();
	settingsLaout->addLayout(buttonLayout);

	stack->addWidget(settingsWidget);
	settingsLaout->addWidget(new QSizeGrip(this), 0, Qt::AlignBottom | Qt::AlignRight);

	// pass application settings to ui
	walletIdText = settingsMan->getValue("wallet_id", "").toString();
	walletEdit->setText(walletIdText);
	poolText = settingsMan->getValue("pool", "").toString();
	poolEdit->setText(poolText);
	passwordText = settingsMan->getValue("password", "").toString();
	passwordEdit->setText(passwordText);
	identifierText = settingsMan->getValue("identifier", "").toString();
	identifierEdit->setText(identifierText);
	
}

void MinerUI::configureConnections()
{
	connect(settings, &QAction::triggered, [this]() {
		stack->setCurrentIndex(1);
		
	});

	connect(back, &QAction::triggered, [this]() {
		stack->setCurrentIndex(0);

	});

	


	connect(currency, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {

	});

	connect(advance, SIGNAL(triggered(bool)), this, SLOT(switchToAdvanceMode()));
	connect(close, &QAction::triggered, [this]() {
		hide();
	});

	connect(startBtn, &QPushButton::clicked, [this]() {
		if (!mining) {
			startMining();
		}
		else {
			stopMining();
		}

	});

	connect(autoStartSwitch, &MSwitch::switchPressed, [this](bool val) {
		startAutomatically = val;
		this->settingsMan->setValue("miner_auto_start", val);
	});

	foreach(card, list) {
		connect(card, &GraphicsCardUI::switchIsOn, [this](bool val) {
			//do something;
		});
	}
}

void MinerUI::configureStyleSheet()
{
	setStyleSheet("*{color:rgba(255,255,255)}"
		"QGroupBox, #settingsWidget{ background: rgba(33,33,33,1); margin:0px; padding : 0px; border: 0px solid black; }"
		"QScrollArea, #cardHolder{ border: 1px solid rgba(130,130,130,0); background: rgba(17,17,17,0); border-radius:1px; }"
		"#cardHolder, #grip {background: rgba(17,17,17,0); padding: 0px; margin: 0px; }"
		"QLabel{ color: rgba(255,255,255,.9); }"
		"QLabel#label{ padding-left: 10px; background:rgba(10,10,10,0); }"
		"QToolButton, #back {border-radius: 1px; background: rgba(20,20,20, 0); color: rgba(250,250,250, 1); border : 0px solid rgba(20,20,20, 1); padding: 4px 6px 4px 6px ; margin-right:3px;}"
		//     "QToolButton:hover{background: rgba(48,48,48, 1);}"
		"QScrollBar::handle {background: rgba(40,128, 185,.9); border-radius: 4px; right: 1px; width: 8px;}"
		"QScrollBar{border : 0px solid black; background-color: rgba(32,32,32,.1); width: 8px;padding: 1px;}"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {background: rgba(200,200,200,0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical { background: rgba(0,0,0,0); border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {background: rgba(10,0,0,.1);}"
		"QPushButton{ background: rgba(20,20,20,1); border: 1px solid rgba(10,10,10,1); border-radius: 1px;  color: rgba(255,255,255,.9); padding : 3px 9px 3px 9px; }"
		""
		"#startBtn{ padding: 9px 19px 9px 19px; background:rgba(23,23,23,.7); border:1px solid rgba(0,0,0,0);}"
		"#startBtn:hover, QToolButton:hover, #back:hover { background : rgba(40,128, 185,.9); }"
		//   "QScrollArea{background: rgba(23,23,23,1); border: 0px solid black; }"
		"#toolBar{ background: rgba(40,128, 185,0); border: 1px solid rgba(10,0,0,0); }"
		//"#back{ background: rgba(40,128, 185,0); border: 0px solid rgba(40,40,40,0.3); }"
		"#bottomBtn{border: 1px solid rgba(40,40,40,0.3); padding: 10px; }"
		"#bottomBtn:hover{background: rgba(40,128, 185,0.5);}"
		"#edit { background: rgba(17,17,17,1); margin-left :10; margin-right : 10px; border : 0px; border-bottom : 1px solid black; }"
		"#currencyBox, #currencyBox:drop-down {background-color: rgba(33,33,33,1); border :0px; border-bottom: 1px solid black; padding-left: 10px; margin-left : 5px; }"
		"#currencyBox QAbstractItemView {background-color: rgba(33,33,33,1); border :0px; border-bottom: 1px solid black; padding-left: 10px; margin-left : 5px; selection-background-color: rgba(40,128, 185,0); }"
		"#currencyBox QAbstractItemView::item:hover {background-color: rgba(40,128,185,1); border :0px;  }"
		""
		"");
}

void MinerUI::saveAndApplySettings()
{
	walletIdText = walletEdit->text();
	settingsMan->setValue("wallet_id", walletIdText);
	poolText = poolEdit->text();
	settingsMan->setValue("pool", poolText);
	passwordText = passwordEdit->text();
	settingsMan->setValue("password", passwordText);
	identifierText = identifierEdit->text();
	settingsMan->setValue("identifier", identifierText);

	minerMan->walletId = walletIdText;
	minerMan->poolUrl = poolText;
	minerMan->password = passwordText;
	minerMan->identifier = identifierText;

	//restart mining
	restartMining();
}

void MinerUI::restoreSettings()
{
	settingsMan->setValue("wallet_id", walletIdText);
	settingsMan->setValue("pool", poolText);
	settingsMan->setValue("password", passwordText);
	settingsMan->setValue("identifier", identifierText);
}

void MinerUI::restartMining()
{
	this->stopMining();
	this->startMining();
}

void MinerUI::startMining()
{
	foreach(card, list) card->setStarted(!mining);

	startBtn->setText("Stop");
	mining = true;
}

void MinerUI::stopMining()
{
	foreach(card, list) card->setStarted(!mining);

	startBtn->setText("Start");
	mining = false;
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

// graphics card functions

GraphicsCardUI::GraphicsCardUI(QWidget *parent = Q_NULLPTR) : QWidget(parent) {
    configureCard();
    configureConnections();
    setColor((int)Connection::NOTCONNECTED);
    contract();
}

void GraphicsCardUI::setCardName(QString name) {
        cardName->setText(name);
        repaint();
}

void GraphicsCardUI::expand() {
        additional->show();
}

void GraphicsCardUI::contract() {
        additional->hide();
}

void GraphicsCardUI::setSpeed(double rate) {
        speed->setText("Speed: " + QString::number(rate) +" H/s");
}

void GraphicsCardUI::setArmed(bool armed) {
        this->armed = armed;
}

void GraphicsCardUI::setDotColor(int con) {
        setColor(con);
        dot->setColor(color);
}

void GraphicsCardUI::hideLabel() {
        displayLabel->hide();
}

void GraphicsCardUI::showLabel() {
        displayLabel->show();
}

void GraphicsCardUI::setStarted(bool val) {
        if (armed && val)
        {
                mining = val;
                if (process != nullptr) {
                        process->startMining();
                        setHighlight(true);
                }
        }
        else {
                if (process != nullptr) {
                        if (process->isMining()) {
                                process->stopMining();
                                setHighlight(false);
                        }
                }
        }
}

void GraphicsCardUI::setHighlight(bool val) {
        logo->setCheckable(true);
        logo->setChecked(val);
        displayLabel->setText(val? "--Mining--": oldString);
}

void GraphicsCardUI::setMinerProcess(MinerProcess* process)
{
        this->process = process;

        if (process != nullptr) {
                connect(process, &MinerProcess::onMinerChartData, [this](MinerChartData data)
                {
                        // set last hash to ui
                        this->setSpeed(data.hps);

                        // if hps is 0 then it must be connecting
                        // set pool color to orange
                        if (data.connected)
                                this->setDotColor((int)Connection::CONNECTED);
                        else
                                this->setDotColor((int)Connection::CONNECTING);

                        if (data.hps != 0) {
                                if (this->info->data.size() > 100)
                                        this->info->data.removeFirst();
                                this->info->data.append(data);
                                this->info->repaint();
                        }
                });

                connect(process, &MinerProcess::minerStatusChanged, [this](MinerStatus status)
                {
                        switch (status)
                        {
                        case MinerStatus::Idle:
                                this->setDotColor((int)Connection::INACTIVE);
                                break;
                        case MinerStatus::Starting:
                                this->setDotColor((int)Connection::CONNECTING);
                                break;
                        case MinerStatus::Mining:
                                this->setDotColor((int)Connection::CONNECTED);
                                break;
                        case MinerStatus::Stopping:
                                this->setDotColor((int)Connection::NOTCONNECTED);
                                break;
                        }
                });
        }
}

void GraphicsCardUI::startMining()
{
        if (armed) process->startMining();
}

void GraphicsCardUI::stopMining()
{
        process->stopMining();
}

void GraphicsCardUI::setColor(int num) {
        switch (num) {
        case 1:
                color.setRgb(0, 120, 0, 255);
                break;
        case 2:
                color.setRgb(255, 120, 70, 255);
                break;
        case 3:
                color.setRgb(170, 1, 2, 255);
                break;
        case 4:
                color.setRgb(240, 240, 240, 255);
                break;
        }
}

void GraphicsCardUI::configureCard() {
        // QVBoxLayout *cardLayout = new QVBoxLayout;
        QHBoxLayout *mainLayout = new QHBoxLayout;
        QHBoxLayout *sliderLayout = new QHBoxLayout;
        QVBoxLayout *logoLayout = new QVBoxLayout;
        QVBoxLayout *infoLayout = new QVBoxLayout;
        auto poolDotLayout = new QHBoxLayout;

        auto card = new QWidget;
        auto cardLayout = new QGridLayout;
        card->setLayout(cardLayout);
        card->setObjectName(QStringLiteral("card"));
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto toolBar = new QWidget;
        toolBar->setObjectName(QStringLiteral("cardBar"));
        auto toolBarLayout = new QHBoxLayout;
        toolBar->setLayout(toolBarLayout);
        toolBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        logo = new QPushButton();
        logo->setObjectName(QStringLiteral("logo"));
        logo->setLayout(logoLayout);
        logo->setFixedSize(150, 98);
        //logo->setCheckable(true);
        //logo->setCursor(Qt::PointingHandCursor);
        logo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        //logo->blockSignals(true);


        pool = new QLabel("Pool  ");
        pool->setAlignment(Qt::AlignLeft);
        dot = new Dot(color);
        poolDotLayout->addWidget(pool);
        poolDotLayout->addWidget(dot);
        poolDotLayout->setSpacing(2);
        setDotColor((int)Connection::CONNECTED);

        speed = new QLabel("Speed: ");
        speed->setAlignment(Qt::AlignLeft);
        cardName = new QLabel("AMD A9");
        cardName->setAlignment(Qt::AlignHCenter);
        cardName->setWordWrap(true);

        QFont font = cardName->font();
        font.setStyleStrategy(QFont::PreferAntialias);
        //font.setBold(true);
        cardName->setFont(font);
        font.setBold(false);
        font.setPixelSize(12);
        pool->setFont(font);
        speed->setFont(font);

        switchBtn = new MSwitch();
        switchBtn->setColor(QColor(40, 128, 185));
        switchBtn->setSizeOfSwitch(22);
        sliderLayout->addStretch();
        sliderLayout->addWidget(switchBtn);
        sliderLayout->addStretch();

        //logoLayout->addStretch();
        logoLayout->addWidget(cardName);
        logoLayout->addLayout(poolDotLayout);
        logoLayout->addWidget(speed);
        logoLayout->addLayout(sliderLayout);
        //logoLayout->addStretch();

        //info = new QWidget();
        info = new MinerChart();
        info->setObjectName(QStringLiteral("info"));
        info->setLayout(infoLayout);
        info->setFixedHeight(98);
        info->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        additional = new QWidget();
        additional->setObjectName(QStringLiteral("additional"));
        additional->setMinimumHeight(80);
        additional->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        displayLabel = new QLabel("GPU is not set to mined");
        displayLabel->setAlignment(Qt::AlignHCenter);
        displayLabel->setObjectName(QStringLiteral("gpuLabel"));
        font.setBold(true);
        displayLabel->setFont(font);
        infoLayout->addWidget(displayLabel);
        oldString = displayLabel->text();

        toolBarLayout->addWidget(logo);
        toolBarLayout->addWidget(info);

        cardLayout->addWidget(toolBar);
        cardLayout->addWidget(additional);

        // mainLayout->addWidget(logo);
        // mainLayout->addWidget(info);
        //        cardLayout->addLayout(mainLayout);
        //        cardLayout->addWidget(additional);
        //        cardLayout->setSpacing(6);
        //cardLayout->setSizeConstraint(QLayout::SetFixedSize);
        cardLayout->setContentsMargins(2, 1, 3, 2);
        setLayout(mainLayout);
        mainLayout->addWidget(card);

        qDebug() << card->geometry();
        setStyleSheet(" * {color: white; }"
                "QWidget#logo, QWidget#info,QWidget#additional  { background:rgba(17,17,17,0); border : 0px solid rgba(00,00,00,.2); border-radius: 1px; margin: 0px;  }"
                //"QWidget#logo:hover, QWidget#info:hover,QWidget#additional:hover{border : 1px solid rgba(40,128,185,.01); }"
                //"QLabel{ color:rgba(255,255,255,.8); padding :3px; }"
                "QWidget#info  { border-left : 5px solid rgba(00,00,00,.1);  }"
                "#card:hover{background: rgba(40,128,185,.11); }"
                "#logo:checked {background: qlineargradient(x1: 0, y1: 1, x2: 1, y2: 1, stop: 0 rgba(40,128,185,.5), stop: 0.5 rgba(17,17,17,0));}"
                "QToolBar::separator{ background:rgba(0,0,0,.1);}"
                "#card{background: rgba(40,128,185,.1); border: 1px solid rgba(0,0,0,.85);}"
                "#gpuLabel{ color: rgba(150,150,170,.8);}");

        QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
        effect->setBlurRadius(10);
        effect->setXOffset(0);
        effect->setYOffset(0);
        effect->setColor(QColor(0, 0, 0));
        //  card->setGraphicsEffect(effect);
        //additional->setGraphicsEffect(effect);
        //logo->setGraphicsEffect(effect);
}

void GraphicsCardUI::configureConnections() {

        connect(switchBtn, &MSwitch::switchPressed, [this](bool val) {
                emit switchIsOn(val);
                armed = val;
        //	logo->setChecked(val);
                displayLabel->setText(val ? "GPU set to mined" : "GPU is not set to mine");
                oldString = displayLabel->text();
        });

        //connect(logo, &QPushButton::clicked, [this]() {
        //	emit switchIsOn(logo->isChecked());
        //	armed = logo->isChecked();
        //	if (logo->isChecked()) {
        //	//	displayLabel->setText("GPU set to mined");
        //		switchBtn->toggle();

        //	}
        //	else {
        //	//	displayLabel->setText("GPU is not set to mine");
        //		switchBtn->toggle();

        //	}
        //});

        connect(logo, &QPushButton::clicked, [=]() {
                logo->setChecked(false);
                switchBtn->toggle();
        });
}




// dot class functions

Dot::Dot(QColor col, QWidget *parent = Q_NULLPTR) :QWidget(parent) {
    setFixedSize(15, 15);
    color = col;
}

void Dot::setColor(QColor col) {
    color = col;
    repaint();
}

void Dot::paintEvent(QPaintEvent *event){
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::HighQualityAntialiasing);
    painter.setPen(QPen(color, 6));
    painter.drawEllipse(3, 4, 7, 7);
}



