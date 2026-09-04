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
// collapsible tool rows, inline images), an input box (Enter sends,
// Shift+Enter newline) and Send/Stop buttons, plus three special states:
// the friendly "install Claude Code" state when the CLI is absent, the
// "open a project" state, and an "MCP off" banner offering to enable it.
//
// B3 (CLAUDE_EDITOR_SPEC §E): the transcript renders what the stream already
// carried and the window used to drop —
//   * TOOL ROWS read as work, not as protocol: "taking a screenshot" plus a
//     one-line argument digest, with the full JSON one click away. The raw
//     `mcp__jahshaka__screenshot` name is still in the detail block.
//   * IMAGES. `screenshot` and `browse_assets` return real PNGs and the dock
//     showed the string "(image)" — the one place it was poorer than a
//     terminal. They are now scaled into the transcript, click for full size.
//   * COST. `result.total_cost_usd` was parsed and thrown away; it is a muted
//     per-turn line in the header (the dock's model is not free).
//   * DENIALS. `result.permission_denials` (§C rung b) becomes a row that says
//     the editor is reached through scripting only — the lockdown, explained
//     at the moment it bites, instead of an invisible refusal.
//   * A "thought for a moment" affordance, a rate-limit row, and `parseError`
//     finally connected (garbage used to vanish silently).
// The MODEL PICKER sits in the header: it writes the `claude_model` setting
// MainWindow already reads, and applies to the NEXT conversation — a live
// session keeps the model it launched with, so the row says so.
//
// Colors are a self-contained dark palette (explicit on every widget) so the
// popup reads identically under Qlementine dark and stays legible under
// Classic — it deliberately opts out of both themes' cascades.

#include <QStringList>
#include <QWidget>

#include "scripting/claude/claudecliprobe.h"

class ClaudeChatHost;
class QComboBox;
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

    /// The friendly label for a tool row: "mcp__jahshaka__screenshot" reads as
    /// "taking a screenshot". Unknown tools keep their own name.
    static QString toolLabel(const QString &toolName);
    /// A one-line digest of a tool's input JSON for the row: the interesting
    /// values, whitespace collapsed, bounded. Never the whole script.
    static QString compactArgs(const QString &inputJson);

    // Introspection for tests and callers.
    bool isInstallStateVisible() const;
    bool isMcpBannerVisible() const;
    int messageCount() const { return mMessageCount; }
    /// Images rendered inline in the transcript so far.
    int imageCount() const { return mImageCount; }
    /// The header's per-turn cost line ("$0.043"), empty before the first turn.
    QString costText() const;
    /// The tool rows' visible text, in order.
    QStringList toolLines() const { return mToolLines; }
    /// The muted info rows' text, in order (denials, rate limits, notices).
    QStringList infoLines() const { return mInfoLines; }
    /// The model id the picker currently shows.
    QString selectedModel() const;

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
    void addImage(const QByteArray &imageData, const QString &mimeType);
    void applyModelChoice(const QString &modelId, bool announce);
    void beginAssistantBubble();
    void appendAssistantDelta(const QString &text);
    void finalizeAssistantBubble(const QString &fullText);
    void scrollToBottom();

    void sendCurrentInput();
    void updateBusyUi(bool busy);
    void clearConversation();
    /// The transcript rows only — no session change (see the .cpp for why the
    /// project-switch path must not clear the session).
    void clearTranscript();

    QSettings *mSettings = nullptr;
    ClaudeChatHost *mHost = nullptr;

    // Header / states / chrome.
    QLabel *mStatusLabel = nullptr;
    QLabel *mCostLabel = nullptr;
    QComboBox *mModelCombo = nullptr;
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
    int mImageCount = 0;
    bool mStopRequested = false;   // the user pressed Stop: the turn's failure
                                   // result is "stopped", not an error
    QStringList mToolLines;
    QStringList mInfoLines;
    QPoint mDragOffset;
    bool mDragging = false;
};

#endif // CLAUDECHATWINDOW_H
