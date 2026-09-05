/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "claudechatwindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "scripting/claude/claudechathost.h"
#include "scripting/claude/claudelaunchconfig.h"

namespace {

const char *kGeometryKey = "claude_chat/geometry";
// The same key MainWindow::toggleClaudeChat reads when it builds the host, so
// a choice made here survives a close/reopen and an app restart.
const char *kModelKey = "claude_model";

// Inline images are bounded by the popup, not by the PNG: a browse_assets
// answer can carry two dozen of them.
const int kInlineImageMaxWidth = 320;
const int kInlineImageMaxHeight = 260;

// Self-contained dark palette — identical under Qlementine dark and Classic.
const char *kWindowStyle = R"(
QWidget#claudeChatRoot {
    background: #202124; border: 1px solid #3c3f44; border-radius: 6px;
}
QWidget#claudeChatHeader { background: #2a2c30; border-bottom: 1px solid #3c3f44; }
QLabel { color: #e8e8ea; background: transparent; }
QLabel#claudeTitle { font-weight: bold; color: #e8e8ea; }
QLabel#claudeStatus { color: #9aa0a6; font-size: 11px; }
QLabel#claudeCost { color: #7d8288; font-size: 11px; }
QLabel#claudeInfoLine { color: #9aa0a6; font-size: 11px; font-style: italic; }
QLabel#claudeImage { background: #26272b; border: 1px solid #3c3f44; border-radius: 6px; }
QComboBox#claudeModel {
    background: #35373c; color: #b8bcc2; border: 1px solid #4a4d52;
    border-radius: 4px; padding: 1px 6px; font-size: 11px;
}
QComboBox#claudeModel QAbstractItemView {
    background: #2a2c30; color: #e8e8ea; selection-background-color: #3d6db5;
}
QLabel#claudeBubbleUser {
    background: #2f4f77; color: #eef2f8; border-radius: 8px; padding: 7px 10px;
}
QLabel#claudeBubbleAssistant {
    background: #2e3035; color: #e8e8ea; border-radius: 8px; padding: 7px 10px;
}
QLabel#claudeBubbleError {
    background: #4a2b2b; color: #ffb3ab; border-radius: 8px; padding: 7px 10px;
}
QLabel#claudeToolDetail {
    background: #26272b; color: #b8bcc2; border-radius: 6px; padding: 6px;
    font-family: monospace; font-size: 11px;
}
QToolButton#claudeToolLine {
    color: #8ab4f8; background: transparent; border: none; font-size: 11px;
    text-align: left; padding: 1px;
}
QScrollArea#claudeScroll, QWidget#claudeMessages { background: #202124; border: none; }
QPlainTextEdit#claudeInput {
    background: #2a2c30; color: #e8e8ea; border: 1px solid #3c3f44;
    border-radius: 6px; padding: 4px; font-size: 13px;
}
QPushButton {
    background: #35373c; color: #e8e8ea; border: 1px solid #4a4d52;
    border-radius: 5px; padding: 5px 14px;
}
QPushButton:hover { background: #3f4247; }
QPushButton:disabled { color: #6a6d72; background: #2c2e32; }
QPushButton#claudeSend { background: #3d6db5; border-color: #3d6db5; }
QPushButton#claudeSend:hover { background: #4a7bc4; }
QPushButton#claudeSend:disabled { background: #2c3a4f; color: #7d8794; }
QPushButton#claudeStop { background: #8a3d3d; border-color: #8a3d3d; }
QToolButton#claudeHeaderBtn {
    color: #b8bcc2; background: transparent; border: none;
    font-size: 13px; padding: 3px;
}
QToolButton#claudeHeaderBtn:hover { color: #ffffff; background: #3c3f44; border-radius: 4px; }
QWidget#claudeMcpBanner { background: #4a4023; border-bottom: 1px solid #5c5230; }
QLabel#claudeMcpBannerText { color: #e8d9a0; font-size: 11px; }
)";

} // namespace

ClaudeChatWindow::ClaudeChatWindow(QSettings *settings, ClaudeChatHost *host, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint), mSettings(settings)
{
    setObjectName(QStringLiteral("claudeChatRoot"));
    setWindowTitle(tr("Claude"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromLatin1(kWindowStyle));
    setMinimumSize(300, 320);
    buildUi();

    // The persisted choice wins over the shipped default; selecting it here
    // must not write an info row (nothing changed for the user).
    if (mSettings && mModelCombo) {
        const QString saved = mSettings->value(QString::fromLatin1(kModelKey)).toString();
        const int index = saved.isEmpty() ? -1 : mModelCombo->findData(saved);
        if (index >= 0) {
            QSignalBlocker block(mModelCombo);
            mModelCombo->setCurrentIndex(index);
        }
    }

    bool restored = false;
    if (mSettings) {
        const QByteArray geometry =
            mSettings->value(QString::fromLatin1(kGeometryKey)).toByteArray();
        if (!geometry.isEmpty()) restored = restoreGeometry(geometry);
    }
    if (!restored) resize(380, 520);

    if (host) setHost(host);
    // Whatever the picker shows is what the host must use — including the
    // shipped default when nothing was ever persisted.
    applyModelChoice(selectedModel(), false);
    refreshStates();
}

ClaudeChatWindow::~ClaudeChatWindow()
{
    saveGeometryNow();
}

void ClaudeChatWindow::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);

    // ---- Header (drag area) ----
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("claudeChatHeader"));
    header->setFixedHeight(34);
    header->setCursor(Qt::SizeAllCursor);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 0, 6, 0);

    auto *title = new QLabel(tr("Claude"), header);
    title->setObjectName(QStringLiteral("claudeTitle"));
    mStatusLabel = new QLabel(header);
    mStatusLabel->setObjectName(QStringLiteral("claudeStatus"));
    headerLayout->addWidget(title);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(mStatusLabel, 1);

    // Per-turn cost (result.total_cost_usd, previously parsed and dropped).
    mCostLabel = new QLabel(header);
    mCostLabel->setObjectName(QStringLiteral("claudeCost"));
    mCostLabel->setToolTip(tr("What the last turn cost on your Claude account"));
    headerLayout->addWidget(mCostLabel);

    // Model picker. The owner's default leads the list; a change applies to
    // the NEXT conversation because the argv is built when the process starts.
    mModelCombo = new QComboBox(header);
    mModelCombo->setObjectName(QStringLiteral("claudeModel"));
    mModelCombo->setCursor(Qt::PointingHandCursor);
    mModelCombo->setToolTip(tr("Model for new conversations"));
    for (const auto &choice : ClaudeLaunchConfig::modelChoices())
        mModelCombo->addItem(choice.label, choice.id);
    connect(mModelCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applyModelChoice(selectedModel(), true);
    });
    headerLayout->addWidget(mModelCombo);

    auto *clearBtn = new QToolButton(header);
    clearBtn->setObjectName(QStringLiteral("claudeHeaderBtn"));
    clearBtn->setText(tr("Clear"));
    clearBtn->setToolTip(tr("Clear the conversation and start a fresh session"));
    connect(clearBtn, &QToolButton::clicked, this, &ClaudeChatWindow::clearConversation);
    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName(QStringLiteral("claudeHeaderBtn"));
    closeBtn->setText(QStringLiteral("✕"));
    closeBtn->setToolTip(tr("Close"));
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::close);
    headerLayout->addWidget(clearBtn);
    headerLayout->addWidget(closeBtn);
    rootLayout->addWidget(header);

    // ---- MCP-off banner ----
    mMcpBanner = new QWidget(this);
    mMcpBanner->setObjectName(QStringLiteral("claudeMcpBanner"));
    auto *bannerLayout = new QHBoxLayout(mMcpBanner);
    bannerLayout->setContentsMargins(10, 4, 6, 4);
    auto *bannerText = new QLabel(
        tr("Jahshaka's MCP server is off — Claude can chat but cannot drive the editor."),
        mMcpBanner);
    bannerText->setObjectName(QStringLiteral("claudeMcpBannerText"));
    bannerText->setWordWrap(true);
    auto *enableBtn = new QPushButton(tr("Enable"), mMcpBanner);
    connect(enableBtn, &QPushButton::clicked, this, &ClaudeChatWindow::enableMcpRequested);
    bannerLayout->addWidget(bannerText, 1);
    bannerLayout->addWidget(enableBtn);
    rootLayout->addWidget(mMcpBanner);
    mMcpBanner->hide();

    // ---- Stacked states ----
    mStack = new QStackedWidget(this);
    rootLayout->addWidget(mStack, 1);

    // Page 0: the chat.
    auto *chatPage = new QWidget(mStack);
    auto *chatLayout = new QVBoxLayout(chatPage);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(4);

    mScroll = new QScrollArea(chatPage);
    mScroll->setObjectName(QStringLiteral("claudeScroll"));
    mScroll->setWidgetResizable(true);
    mScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *messagesHost = new QWidget(mScroll);
    messagesHost->setObjectName(QStringLiteral("claudeMessages"));
    mMessages = new QVBoxLayout(messagesHost);
    mMessages->setContentsMargins(8, 8, 8, 8);
    mMessages->setSpacing(6);
    mMessages->addStretch(1);
    mScroll->setWidget(messagesHost);
    chatLayout->addWidget(mScroll, 1);

    auto *inputRow = new QWidget(chatPage);
    auto *inputLayout = new QHBoxLayout(inputRow);
    inputLayout->setContentsMargins(8, 0, 4, 4);
    inputLayout->setSpacing(6);
    mInput = new QPlainTextEdit(inputRow);
    mInput->setObjectName(QStringLiteral("claudeInput"));
    mInput->setPlaceholderText(tr("Ask Claude to build something…"));
    mInput->setFixedHeight(64);
    mInput->installEventFilter(this);
    auto *buttonColumn = new QVBoxLayout();
    buttonColumn->setSpacing(4);
    mSendButton = new QPushButton(tr("Send"), inputRow);
    mSendButton->setObjectName(QStringLiteral("claudeSend"));
    mSendButton->setDefault(true);
    connect(mSendButton, &QPushButton::clicked, this, &ClaudeChatWindow::sendCurrentInput);
    mStopButton = new QPushButton(tr("Stop"), inputRow);
    mStopButton->setObjectName(QStringLiteral("claudeStop"));
    mStopButton->hide();
    connect(mStopButton, &QPushButton::clicked, this, [this]() {
        if (!mHost) return;
        // A cooperative interrupt ends the turn with is_error — that is the
        // user's own Stop, not a failure, so do not paint it red.
        mStopRequested = true;
        mHost->stopTurn();
    });
    buttonColumn->addWidget(mSendButton);
    buttonColumn->addWidget(mStopButton);
    buttonColumn->addStretch(1);
    auto *grip = new QSizeGrip(inputRow);
    buttonColumn->addWidget(grip, 0, Qt::AlignRight | Qt::AlignBottom);
    inputLayout->addWidget(mInput, 1);
    inputLayout->addLayout(buttonColumn);
    chatLayout->addWidget(inputRow);
    mStack->addWidget(chatPage);

    // Page 1: install Claude Code.
    auto *installPage = new QWidget(mStack);
    auto *installLayout = new QVBoxLayout(installPage);
    installLayout->addStretch(1);
    auto *installTitle = new QLabel(tr("<b>Claude Code isn't installed</b>"), installPage);
    installTitle->setAlignment(Qt::AlignCenter);
    auto *installBody = new QLabel(
        tr("The chat runs on the Claude Code CLI and its login.<br><br>"
           "Install it from <b>claude.com/claude-code</b><br>"
           "then sign in by running <b>claude</b> once in a terminal<br>"
           "and reopen this window."),
        installPage);
    installBody->setAlignment(Qt::AlignCenter);
    installBody->setWordWrap(true);
    mInstallDetail = new QLabel(installPage);
    mInstallDetail->setObjectName(QStringLiteral("claudeStatus"));
    mInstallDetail->setAlignment(Qt::AlignCenter);
    mInstallDetail->setWordWrap(true);
    auto *retryBtn = new QPushButton(tr("Check again"), installPage);
    connect(retryBtn, &QPushButton::clicked, this,
            [this]() { setCliState(ClaudeCliProbe::probe()); });
    installLayout->addWidget(installTitle);
    installLayout->addSpacing(6);
    installLayout->addWidget(installBody);
    installLayout->addSpacing(6);
    installLayout->addWidget(mInstallDetail);
    installLayout->addSpacing(10);
    installLayout->addWidget(retryBtn, 0, Qt::AlignCenter);
    installLayout->addStretch(2);
    mStack->addWidget(installPage);

    // Page 2: no project open.
    auto *noProjectPage = new QWidget(mStack);
    auto *noProjectLayout = new QVBoxLayout(noProjectPage);
    auto *noProjectLabel = new QLabel(
        tr("Open a project to chat —\nClaude works inside the open project."), noProjectPage);
    noProjectLabel->setAlignment(Qt::AlignCenter);
    noProjectLabel->setWordWrap(true);
    noProjectLayout->addStretch(1);
    noProjectLayout->addWidget(noProjectLabel);
    noProjectLayout->addStretch(2);
    mStack->addWidget(noProjectPage);
}

void ClaudeChatWindow::setHost(ClaudeChatHost *host)
{
    if (mHost == host) return;
    if (mHost) {
        disconnect(mHost, nullptr, this, nullptr);
        disconnect(mHost->parser(), nullptr, this, nullptr);
    }
    mHost = host;
    if (mHost) connectHost();
    refreshStates();
}

void ClaudeChatWindow::connectHost()
{
    ClaudeStreamParser *parser = mHost->parser();
    connect(parser, &ClaudeStreamParser::textBlockStarted, this,
            &ClaudeChatWindow::beginAssistantBubble);
    connect(parser, &ClaudeStreamParser::textDelta, this,
            &ClaudeChatWindow::appendAssistantDelta);
    connect(parser, &ClaudeStreamParser::assistantText, this,
            &ClaudeChatWindow::finalizeAssistantBubble);
    connect(parser, &ClaudeStreamParser::toolUseStarted, this,
            &ClaudeChatWindow::addToolLine);
    connect(parser, &ClaudeStreamParser::toolResult, this,
            [this](const QString &snippet, bool isError) {
                if (isError) {
                    auto *bubble = addBubble(tr("Tool error: %1").arg(snippet), false);
                    bubble->setObjectName(QStringLiteral("claudeBubbleError"));
                    bubble->style()->unpolish(bubble);
                    bubble->style()->polish(bubble);
                }
            });
    // The screenshots and thumbnails the tools return — the transcript's one
    // remaining gap versus a terminal.
    connect(parser, &ClaudeStreamParser::toolResultImage, this, &ClaudeChatWindow::addImage);
    connect(parser, &ClaudeStreamParser::thinkingBlock, this,
            [this]() { addInfoLine(tr("thought for a moment")); });
    connect(parser, &ClaudeStreamParser::rateLimitEvent, this,
            [this](const QString &status, const QString &resetsAt) {
                addInfoLine(resetsAt.isEmpty()
                                ? tr("rate limit: %1").arg(status)
                                : tr("rate limit: %1 — resets at %2").arg(status, resetsAt));
            });
    // §C rung b: the lockdown explained at the moment it bites.
    connect(parser, &ClaudeStreamParser::permissionsDenied, this,
            [this](const QStringList &tools) {
                addInfoLine(tr("blocked: %1 — the editor is reached through scripting only")
                                .arg(tools.join(QStringLiteral(", "))));
            });
    // parseError was emitted and never connected: garbage on the stream simply
    // vanished, which is the worst way to debug a CLI that changed its output.
    connect(parser, &ClaudeStreamParser::parseError, this, [this](const QString &line) {
        addInfoLine(tr("unreadable line from Claude Code: %1").arg(line.left(80)));
    });
    connect(parser, &ClaudeStreamParser::sessionStarted, this,
            [this](const QString &, const QStringList &, const QStringList &, bool mcpConnected) {
                mStatusLabel->setText(mcpConnected ? tr("connected to the editor")
                                                   : (mMcpRunning ? tr("MCP not connected")
                                                                  : tr("editor control off")));
            });
    connect(parser, &ClaudeStreamParser::turnCompleted, this,
            [this](bool ok, const QString &resultText, const QString &, double costUsd) {
                mStreamingBubble = nullptr;
                mStreamingText.clear();
                if (costUsd > 0.0 && mCostLabel)
                    mCostLabel->setText(QStringLiteral("$%1").arg(costUsd, 0, 'f', 3));
                if (ok) {
                    mStopRequested = false;
                    return;
                }
                if (mStopRequested) {
                    // The user's own interrupt came back as an error result;
                    // the next send resumes this same session.
                    mStopRequested = false;
                    addInfoLine(tr("stopped"));
                    return;
                }
                auto *bubble = addBubble(
                    resultText.isEmpty() ? tr("The turn failed.") : resultText, false);
                bubble->setObjectName(QStringLiteral("claudeBubbleError"));
                bubble->style()->unpolish(bubble);
                bubble->style()->polish(bubble);
            });
    connect(mHost, &ClaudeChatHost::busyChanged, this, &ClaudeChatWindow::updateBusyUi);
    connect(mHost, &ClaudeChatHost::processFailed, this, [this](const QString &detail) {
        auto *bubble = addBubble(tr("Claude Code failed: %1").arg(detail), false);
        bubble->setObjectName(QStringLiteral("claudeBubbleError"));
        bubble->style()->unpolish(bubble);
        bubble->style()->polish(bubble);
    });
    connect(mHost, &ClaudeChatHost::turnAborted, this, [this]() {
        mStopRequested = false;   // the kill fallback fired; no result is coming
        addInfoLine(tr("stopped"));
    });
    // D3: the previous conversation could not be resumed. The host has already
    // dropped the id and restarted with the user's message — say so, so the
    // lost context is visible rather than mysterious.
    connect(mHost, &ClaudeChatHost::sessionResumeFailed, this, [this]() {
        mResumeNoticeShown = true;
        addInfoLine(tr("the previous conversation could not be resumed — starting a new one"));
    });
    // D1: a project switch ends the conversation (new cwd, new MCP config, new
    // session file). The transcript above belongs to the old project.
    connect(mHost, &ClaudeChatHost::projectChanged, this, [this](const QString &folder) {
        clearTranscript();
        if (!folder.isEmpty())
            addInfoLine(tr("switched to %1 — new conversation")
                            .arg(QFileInfo(folder).fileName()));
        else
            addInfoLine(tr("the project was closed — new conversation next time"));
    });
}

// ---- state pages ----

void ClaudeChatWindow::setCliState(const ClaudeCliProbe::Result &result)
{
    mCliFound = result.status == ClaudeCliProbe::Status::Found;
    if (!mCliFound && mInstallDetail) {
        mInstallDetail->setText(result.status == ClaudeCliProbe::Status::Error
                                    ? tr("`%1 --version` failed: %2").arg(result.program, result.detail)
                                    : tr("(`%1` was not found on PATH)").arg(result.program));
    }
    if (mCliFound) mStatusLabel->setText(tr("Claude Code %1").arg(result.version));
    refreshStates();
}

void ClaudeChatWindow::setProjectOpen(bool open)
{
    mProjectOpen = open;
    refreshStates();
}

void ClaudeChatWindow::setMcpRunning(bool running)
{
    mMcpRunning = running;
    refreshStates();
}

void ClaudeChatWindow::refreshStates()
{
    if (!mStack) return;
    if (!mCliFound)
        mStack->setCurrentIndex(1);
    else if (!mProjectOpen)
        mStack->setCurrentIndex(2);
    else
        mStack->setCurrentIndex(0);
    mMcpBanner->setVisible(mCliFound && mProjectOpen && !mMcpRunning);

    if (mCliFound && mProjectOpen && !mResumeNoticeShown && mHost
        && !mHost->sessionId().isEmpty()) {
        mResumeNoticeShown = true;
        addInfoLine(tr("previous conversation resumed — context retained"));
    }
}

bool ClaudeChatWindow::isInstallStateVisible() const
{
    return mStack && mStack->currentIndex() == 1;
}

bool ClaudeChatWindow::isMcpBannerVisible() const
{
    return mMcpBanner && mMcpBanner->isVisibleTo(const_cast<ClaudeChatWindow *>(this));
}

QString ClaudeChatWindow::costText() const
{
    return mCostLabel ? mCostLabel->text() : QString();
}

QString ClaudeChatWindow::selectedModel() const
{
    if (!mModelCombo) return ClaudeLaunchConfig::defaultModel();
    return mModelCombo->currentData().toString();
}

// A live `claude` keeps the model it launched with (the argv is built at
// start), so this is honest about when it takes effect rather than pretending
// the current conversation changed underneath the user.
void ClaudeChatWindow::applyModelChoice(const QString &modelId, bool announce)
{
    if (modelId.isEmpty()) return;
    if (mHost) mHost->setModel(modelId);
    if (!announce) return;
    // Only a real choice is persisted: writing the shipped default here would
    // pin today's default into the user's settings for ever.
    if (mSettings) mSettings->setValue(QString::fromLatin1(kModelKey), modelId);
    const bool live = mHost && (mHost->isProcessRunning() || !mHost->sessionId().isEmpty());
    addInfoLine(live ? tr("model: %1 — applies to the next conversation (Clear starts one)")
                           .arg(modelId)
                     : tr("model: %1").arg(modelId));
}

// ---- message list ----

QWidget *ClaudeChatWindow::addBubble(const QString &text, bool user)
{
    auto *row = new QWidget();
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    auto *bubble = new QLabel(text, row);
    bubble->setObjectName(user ? QStringLiteral("claudeBubbleUser")
                               : QStringLiteral("claudeBubbleAssistant"));
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble->setMaximumWidth(560);
    if (user) {
        rowLayout->addStretch(1);
        rowLayout->addWidget(bubble);
    } else {
        rowLayout->addWidget(bubble);
        rowLayout->addStretch(1);
    }
    mMessages->insertWidget(mMessages->count() - 1, row);
    ++mMessageCount;
    scrollToBottom();
    return bubble;
}

void ClaudeChatWindow::addInfoLine(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("claudeInfoLine"));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    mMessages->insertWidget(mMessages->count() - 1, label);
    mInfoLines << text;
    scrollToBottom();
}

// A transcript reads as work when the rows name the work. The protocol name
// stays available in the expanded detail — this is presentation only.
QString ClaudeChatWindow::toolLabel(const QString &toolName)
{
    static const QString prefix = QStringLiteral("mcp__jahshaka__");
    QString bare = toolName;
    if (bare.startsWith(prefix)) bare = bare.mid(prefix.size());
    if (bare == QLatin1String("run_script")) return tr("running a script");
    if (bare == QLatin1String("screenshot")) return tr("taking a screenshot");
    if (bare == QLatin1String("describe_scene")) return tr("reading the scene");
    if (bare == QLatin1String("browse_assets")) return tr("browsing assets");
    if (bare == QLatin1String("api_docs")) return tr("looking up the API");
    if (bare == QLatin1String("undo_redo")) return tr("stepping undo");
    if (bare == QLatin1String("Skill")) return tr("loading a skill");
    return bare;   // a tool added to the server after this build
}

QString ClaudeChatWindow::compactArgs(const QString &inputJson)
{
    const QJsonObject input =
        QJsonDocument::fromJson(inputJson.toUtf8()).object();
    QStringList parts;
    for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
        QString value;
        const QJsonValue v = it.value();
        if (v.isString()) value = v.toString();
        else if (v.isBool()) value = v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        else if (v.isDouble()) value = QString::number(v.toDouble());
        else if (v.isArray())
            value = QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
        else if (v.isObject())
            value = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        else continue;
        value = value.simplified();
        if (value.size() > 48) value = value.left(45) + QStringLiteral("…");
        // The script itself is the one argument worth showing bare: a key name
        // in front of it wastes the row's width.
        parts << (it.key() == QLatin1String("script")
                      ? value
                      : QStringLiteral("%1: %2").arg(it.key(), value));
    }
    QString joined = parts.join(QStringLiteral(", "));
    if (joined.size() > 90) joined = joined.left(87) + QStringLiteral("…");
    return joined;
}

void ClaudeChatWindow::addImage(const QByteArray &imageData, const QString &mimeType)
{
    QPixmap pixmap;
    if (!pixmap.loadFromData(imageData)) {
        addInfoLine(tr("an image came back that could not be decoded (%1)").arg(mimeType));
        return;
    }
    auto *row = new QWidget();
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto *view = new QLabel(row);
    view->setObjectName(QStringLiteral("claudeImage"));
    const QPixmap scaled =
        (pixmap.width() > kInlineImageMaxWidth || pixmap.height() > kInlineImageMaxHeight)
            ? pixmap.scaled(kInlineImageMaxWidth, kInlineImageMaxHeight,
                            Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : pixmap;
    view->setPixmap(scaled);
    view->setCursor(Qt::PointingHandCursor);
    view->setToolTip(tr("%1×%2 — click for full size").arg(pixmap.width()).arg(pixmap.height()));
    // Full size opens in its own top-level label, owned by nothing else so it
    // closes with the app (and never takes the popup's geometry key).
    view->installEventFilter(this);
    view->setProperty("claudeFullPixmap", pixmap);

    rowLayout->addWidget(view);
    rowLayout->addStretch(1);
    mMessages->insertWidget(mMessages->count() - 1, row);
    ++mImageCount;
    scrollToBottom();
}

void ClaudeChatWindow::addToolLine(const QString &name, const QString &inputJson)
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 0, 0, 0);
    layout->setSpacing(2);

    auto *toggle = new QToolButton(container);
    toggle->setObjectName(QStringLiteral("claudeToolLine"));
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    const QString digest = compactArgs(inputJson);
    const QString rowText = digest.isEmpty()
                                ? toolLabel(name)
                                : QStringLiteral("%1 — %2").arg(toolLabel(name), digest);
    toggle->setText(rowText);
    toggle->setToolTip(name);
    toggle->setCheckable(true);
    toggle->setCursor(Qt::PointingHandCursor);
    mToolLines << rowText;

    // The detail keeps the protocol truth: the namespaced tool name and the
    // whole input, so nothing the friendly row summarised is lost.
    auto *detail = new QLabel(name + QStringLiteral("\n") + inputJson.trimmed(), container);
    detail->setObjectName(QStringLiteral("claudeToolDetail"));
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail->hide();
    connect(toggle, &QToolButton::toggled, this, [toggle, detail](bool on) {
        detail->setVisible(on);
        toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });

    layout->addWidget(toggle);
    layout->addWidget(detail);
    mMessages->insertWidget(mMessages->count() - 1, container);
    // A tool call ends any streaming text bubble above it.
    mStreamingBubble = nullptr;
    mStreamingText.clear();
    scrollToBottom();
}

void ClaudeChatWindow::beginAssistantBubble()
{
    if (mStreamingBubble) return; // consecutive blocks share one bubble
    mStreamingText.clear();
    mStreamingBubble = qobject_cast<QLabel *>(addBubble(QString(), false));
}

void ClaudeChatWindow::appendAssistantDelta(const QString &text)
{
    if (!mStreamingBubble) beginAssistantBubble();
    mStreamingText += text;
    mStreamingBubble->setTextFormat(Qt::PlainText);
    mStreamingBubble->setText(mStreamingText);
    scrollToBottom();
}

void ClaudeChatWindow::finalizeAssistantBubble(const QString &fullText)
{
    QLabel *bubble = mStreamingBubble;
    if (!bubble) bubble = qobject_cast<QLabel *>(addBubble(QString(), false));
    // The complete message replaces the streamed text (authoritative), and
    // upgrades to markdown rendering.
    bubble->setTextFormat(Qt::MarkdownText);
    bubble->setText(fullText);
    mStreamingBubble = nullptr;
    mStreamingText.clear();
    scrollToBottom();
}

void ClaudeChatWindow::scrollToBottom()
{
    QTimer::singleShot(0, this, [this]() {
        if (mScroll)
            mScroll->verticalScrollBar()->setValue(mScroll->verticalScrollBar()->maximum());
    });
}

// ---- input ----

void ClaudeChatWindow::sendCurrentInput()
{
    if (!mHost || mHost->isBusy()) return;
    const QString text = mInput->toPlainText().trimmed();
    if (text.isEmpty() || !mProjectOpen || !mCliFound) return;
    addBubble(text, true);
    mInput->clear();
    mStopRequested = false;
    mHost->sendMessage(text);
}

void ClaudeChatWindow::updateBusyUi(bool busy)
{
    mSendButton->setVisible(!busy);
    mStopButton->setVisible(busy);
    mInput->setEnabled(!busy);
    if (!busy) mInput->setFocus();
}

// The rows only. Split out of clearConversation because a PROJECT SWITCH must
// wipe the transcript WITHOUT calling clearSession() — by the time that signal
// arrives the host already points at the new project, so clearing "the session"
// would delete the NEW project's stored id.
void ClaudeChatWindow::clearTranscript()
{
    // Remove every row but the trailing stretch.
    while (mMessages->count() > 1) {
        QLayoutItem *item = mMessages->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    mStreamingBubble = nullptr;
    mStreamingText.clear();
    mMessageCount = 0;
    mImageCount = 0;
    mStopRequested = false;
    mToolLines.clear();
    mInfoLines.clear();
    if (mCostLabel) mCostLabel->clear();
    mResumeNoticeShown = true; // fresh session — no resume notice
}

void ClaudeChatWindow::clearConversation()
{
    if (mHost) mHost->clearSession();
    clearTranscript();
    addInfoLine(tr("new session"));
}

// ---- window chrome ----

bool ClaudeChatWindow::eventFilter(QObject *watched, QEvent *event)
{
    // An inline image was clicked: show it at its real size in its own window.
    if (event->type() == QEvent::MouseButtonRelease && watched != mInput) {
        const QVariant full = watched->property("claudeFullPixmap");
        if (full.canConvert<QPixmap>()) {
            const QPixmap pixmap = full.value<QPixmap>();
            auto *viewer = new QLabel(nullptr, Qt::Tool);
            viewer->setAttribute(Qt::WA_DeleteOnClose);
            viewer->setWindowTitle(tr("Claude — image"));
            viewer->setPixmap(pixmap);
            viewer->resize(pixmap.size());
            viewer->show();
            return true;
        }
    }
    if (watched == mInput && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            sendCurrentInput();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ClaudeChatWindow::mousePressEvent(QMouseEvent *event)
{
    // Drag from the header strip (top 34px).
    if (event->button() == Qt::LeftButton && event->position().y() <= 34) {
        mDragging = true;
        mDragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ClaudeChatWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (mDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - mDragOffset);
        event->accept();
        return;
    }
    mDragging = false;
    QWidget::mouseMoveEvent(event);
}

void ClaudeChatWindow::saveGeometryNow()
{
    if (mSettings) mSettings->setValue(QString::fromLatin1(kGeometryKey), saveGeometry());
}

void ClaudeChatWindow::closeEvent(QCloseEvent *event)
{
    saveGeometryNow();
    QWidget::closeEvent(event);
}

void ClaudeChatWindow::hideEvent(QHideEvent *event)
{
    saveGeometryNow();
    QWidget::hideEvent(event);
}
