
#include "minerui.h"
#include <QDebug>
#include <QGraphicsDropShadowEffect>

MinerUI::MinerUI(QWidget *parent)
    : QWidget(parent)
{
    configureUI();
    configureConnections();
    //setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint |Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
	setWindowFlag(Qt::SubWindow);
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
	
	fontIcon.initFontAwesome();
    setFixedWidth(490);
    setObjectName(QStringLiteral("this"));
    cardHolderLayout = new QVBoxLayout();
    auto bottomLayout = new QHBoxLayout();
    auto autoLayout = new QVBoxLayout();
    layout = new QVBoxLayout();
    setLayout(layout);

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

    toolbar->addAction(settings);
    toolbar->addAction(advance);
    auto empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    //toolbar->addSeparator();
    toolbar->addWidget(empty);
    toolbar->addAction(close);



    scrollHolder= new QScrollArea(this);
	scrollHolder->setContentsMargins(0, 3, 0, 3);
    scrollHolder->setAlignment(Qt::AlignTop);
	scrollHolder->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    cardHolder = new QWidget();
    cardHolder->setObjectName(QStringLiteral("cardHolder"));
    cardHolder->setGeometry(0,0,450,400);


    layout->addWidget(toolbar);
	layout->addSpacing(3);
    layout->addWidget(scrollHolder);

    for(int i=0;i< 2; i++){
        addGraphicsCard("amd "+QString::number(i));
    }

    cardHolder->setLayout(cardHolderLayout);
    cardHolderLayout->setSizeConstraint(QLayout::SetFixedSize);
	cardHolderLayout->setContentsMargins(0, 0, 0, 0);
	cardHolderLayout->setSpacing(2);
	scrollHolder->setWidget(cardHolder);

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
    autoStartSwitch->setColor(QColor(40,128,185));
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

	layout->addSpacing(3);
    layout->addLayout(bottomLayout);
	layout->setSizeConstraint(QLayout::SetFixedSize);
	layout->setSpacing(0);

    setStyleSheet("#this{ background: rgba(17,17,17,1); margin:0px; padding : 0px; }"
		"QScrollArea, #cardHolder{ border: 1px solid rgba(130,130,130,0); background: rgba(0,0,0,0); border-radius:1px; }"
		"#cardHolder{background: rgba(0,0,0,0); padding: 0px; margin: 0px; }"
                  "QLabel{ color: rgba(255,255,255,.9); }"
                  "QToolButton {border-radius: 1px; background: rgba(20,20,20, 0); color: rgba(250,250,250, 1); border : 1px solid rgba(20,20,20, 0.1); padding: 4px 6px 4px 6px ; margin-right:3px;}"
             //     "QToolButton:hover{background: rgba(48,48,48, 1);}"
                  "QScrollBar::handle {background: rgba(40,128, 185,.9); border-radius: 4px; right: 1px; width: 8px;}"
                  "QScrollBar{border : 0px solid black; background-color: rgba(32,32,32,.1); width: 8px;padding: 1px;}"
                  " QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {background: rgba(200,200,200,0);}"
                  "QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical { background: rgba(0,0,0,0); border: 0px solid white;}"
                  "QScrollBar::sub-line, QScrollBar::add-line {background: rgba(10,0,0,.1);}"
                  "QPushButton{ background: rgba(20,20,20,1); border: 1px solid rgba(10,10,10,1); border-radius: 1px;  color: rgba(255,255,255,.9); padding : 3px 9px 3px 9px; }"
		""
		"#startBtn{ padding: 9px 19px 9px 19px; background:rgba(23,23,23,.7); border:1px solid rgba(0,0,0,0);}"
		"#startBtn:hover, QToolButton:hover { background : rgba(40,128, 185,.9); }"
		"QScrollArea{background: rgba(23,23,23,1);}"
		"#toolBar{ background: rgba(17,17,17,1); border-bottom: 0px solid black; }");
}

void MinerUI::configureConnections()
{
   // connect(settings, SIGNAL(toggled(bool)),this,SLOT());
    connect(advance, SIGNAL(triggered(bool)),this,SLOT(switchToAdvanceMode()));
    connect(close,&QAction::triggered,[this](){
        hide();
    });

    connect(startBtn, &QPushButton::clicked,[this](){

    });
    connect(autoStartSwitch,&MSwitch::switchPressed,[this](bool val){
        startAutomatically=val;
    });

    foreach (card, list){
        connect(card,&GraphicsCardUI::switchIsOn,[this](bool val){
           //do something;
        });
    }
}

void MinerUI::switchToAdvanceMode()
{
    if(!isInAdvanceMode){
        foreach (card, list)   card->expand();
        isInAdvanceMode = true;
    }
    else{
        foreach (card, list)   card->contract();
        isInAdvanceMode = false;
    }


}

