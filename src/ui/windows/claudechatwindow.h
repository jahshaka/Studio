/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLAUDECHATWINDOW_H
#define CLAUDECHATWINDOW_H

// ClaudeChatWindow — the floating Claude chat popup (CLAUDE_EDITOR_SPEC
// phase 2, owner amendment 2026-08-31: NOT a dock). A frameless Qt::Tool
// window that floats over the editor, always on top of the app (child of the
// main window), movable by its header, resizable by a size grip, geometry
// persisted under "claude_chat/geometry" in jahsettings.ini.
//
// The window renders a message list (user/assistant bubbles, streamed text,
// collapsible "using <tool>…" lines), an input box (Enter sends,
// Shift+Enter newline) and Send/Stop buttons, plus three special states:
// the friendly "install Claude Code" state when the CLI is absent, the
// "open a project" state, and an "MCP off" banner offering to enable it.
//
// Colors are a self-contained dark palette (explicit on every widget) so the
// popup reads identically under Qlementine dark and stays legible under
// Classic — it deliberately opts out of both themes' cascades.

#include <QWidget>

#include "scripting/claude/claudecliprobe.h"

class ClaudeChatHost;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSettings;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

class ClaudeChatWindow : public QWidget
{
    Q_OBJECT
public:
    /// `settings` (nullable) persists geometry; `host` (nullable, not owned)
    /// may also be attached later via setHost.
    explicit ClaudeChatWindow(QSettings *settings, ClaudeChatHost *host = nullptr,
                              QWidget *parent = nullptr);
    ~ClaudeChatWindow() override;

    void setHost(ClaudeChatHost *host);
    ClaudeChatHost *host() const { return mHost; }

    /// CLI probe outcome — Found shows the chat, otherwise the install state.
    void setCliState(const ClaudeCliProbe::Result &result);
    /// Project + MCP context (drives the hint states and the MCP banner).
    void setProjectOpen(bool open);
    void setMcpRunning(bool running);

    // Introspection for tests and callers.
    bool isInstallStateVisible() const;
    bool isMcpBannerVisible() const;
    int messageCount() const { return mMessageCount; }

signals:
    /// The user clicked "Enable MCP" on the banner.
    void enableMcpRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void buildUi();
    void connectHost();
    void refreshStates();
    void saveGeometryNow();

    // Message list helpers.
    QWidget *addBubble(const QString &text, bool user);
    void addInfoLine(const QString &text);
    void addToolLine(const QString &name, const QString &inputJson);
    void beginAssistantBubble();
    void appendAssistantDelta(const QString &text);
    void finalizeAssistantBubble(const QString &fullText);
    void scrollToBottom();

    void sendCurrentInput();
    void updateBusyUi(bool busy);
    void clearConversation();

    QSettings *mSettings = nullptr;
    ClaudeChatHost *mHost = nullptr;

    // Header / states / chrome.
    QLabel *mStatusLabel = nullptr;
    QStackedWidget *mStack = nullptr;   // page 0 = chat, 1 = install, 2 = no project
    QWidget *mMcpBanner = nullptr;
    QLabel *mInstallDetail = nullptr;

    // Chat page.
    QScrollArea *mScroll = nullptr;
    QVBoxLayout *mMessages = nullptr;
    QLabel *mStreamingBubble = nullptr; // the assistant bubble being streamed
    QString mStreamingText;
    QPlainTextEdit *mInput = nullptr;
    QPushButton *mSendButton = nullptr;
    QPushButton *mStopButton = nullptr;

    // Context.
    bool mCliFound = false;
    bool mProjectOpen = false;
    bool mMcpRunning = false;
    bool mResumeNoticeShown = false;
    int mMessageCount = 0;
    QPoint mDragOffset;
    bool mDragging = false;
};

#endif // CLAUDECHATWINDOW_H
