/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/scriptconsole.h"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "scripting/scriptengine.h"

ScriptConsole::ScriptConsole(ScriptEngine *engine, QWidget *parent)
    : QWidget(parent), mEngine(engine)
{
    setObjectName(QStringLiteral("ScriptConsole"));

    mLog = new QPlainTextEdit(this);
    mLog->setReadOnly(true);
    mLog->setMaximumBlockCount(5000);
    mLog->setFrameStyle(QFrame::NoFrame);

    mInput = new QPlainTextEdit(this);
    mInput->setPlaceholderText(QStringLiteral("JavaScript — Enter runs, Shift+Enter for a newline, Up/Down for history, help() lists verbs"));
    mInput->setFixedHeight(64);
    mInput->setFrameStyle(QFrame::NoFrame);
    mInput->installEventFilter(this);

    auto *runBtn = new QPushButton(QStringLiteral("Run"), this);
    auto *fileBtn = new QPushButton(QStringLiteral("Run File…"), this);
    auto *clearBtn = new QPushButton(QStringLiteral("Clear"), this);
    auto *helpBtn = new QPushButton(QStringLiteral("Help"), this);

    auto *buttons = new QVBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(4);
    buttons->addWidget(runBtn);
    buttons->addWidget(fileBtn);
    buttons->addWidget(clearBtn);
    buttons->addWidget(helpBtn);
    buttons->addStretch(1);

    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(6);
    inputRow->addWidget(mInput, 1);
    inputRow->addLayout(buttons);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(mLog, 1);
    layout->addLayout(inputRow);

    setStyleSheet(QStringLiteral(
        "#ScriptConsole { background-color: #151515; }"
        "QPlainTextEdit { background-color: #1a1a1a; color: #e6e6e6;"
        "  font-family: 'DejaVu Sans Mono', Consolas, monospace; font-size: 12px;"
        "  border: 1px solid #262626; }"
        "QPushButton { background-color: #2b2b2b; color: #e6e6e6; border: 1px solid #3a3a3a;"
        "  padding: 4px 10px; }"
        "QPushButton:hover { background-color: #3a3a3a; }"));

    connect(runBtn, &QPushButton::clicked, this, &ScriptConsole::runInput);
    connect(fileBtn, &QPushButton::clicked, this, &ScriptConsole::chooseAndRunFile);
    connect(clearBtn, &QPushButton::clicked, mLog, &QPlainTextEdit::clear);
    connect(helpBtn, &QPushButton::clicked, this, [this]() {
        appendLine(mEngine->registry().helpText(), QStringLiteral("#8ec6ff"));
    });
    connect(mEngine, &ScriptEngine::consoleOutput, this, [this](const QString &t) {
        appendLine(t);
    });

    appendLine(QStringLiteral("Jahshaka scripting console — api v%1. help() lists the verbs.")
                   .arg(ApiRegistry::apiVersion()),
               QStringLiteral("#8ec6ff"));
}

void ScriptConsole::appendLine(const QString &text, const QString &color)
{
    if (color.isEmpty())
        mLog->appendPlainText(text);
    else
        mLog->appendHtml(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                             .arg(color, text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>"))));
    mLog->verticalScrollBar()->setValue(mLog->verticalScrollBar()->maximum());
}

void ScriptConsole::runInput()
{
    const QString source = mInput->toPlainText().trimmed();
    if (source.isEmpty()) return;

    mHistory.append(source);
    mHistoryPos = mHistory.size();
    mPendingInput.clear();
    mInput->clear();

    appendLine(QStringLiteral("> %1").arg(source), QStringLiteral("#9ad38f"));
    const auto result = mEngine->evaluate(source, QStringLiteral("<console>"));
    if (result.ok) {
        const QString shown = result.toString();
        if (!shown.isEmpty()) appendLine(shown);
    } else {
        appendLine(result.toString(), QStringLiteral("#ff7a6e"));
        if (!result.stack.isEmpty()) appendLine(result.stack, QStringLiteral("#a05a54"));
    }
}

void ScriptConsole::runFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendLine(QStringLiteral("cannot open %1").arg(path), QStringLiteral("#ff7a6e"));
        return;
    }
    appendLine(QStringLiteral("> run %1").arg(path), QStringLiteral("#9ad38f"));
    const auto result = mEngine->evaluate(QString::fromUtf8(file.readAll()), path);
    if (result.ok) {
        const QString shown = result.toString();
        if (!shown.isEmpty()) appendLine(shown);
        appendLine(QStringLiteral("done"), QStringLiteral("#8ec6ff"));
    } else {
        appendLine(result.toString(), QStringLiteral("#ff7a6e"));
        if (!result.stack.isEmpty()) appendLine(result.stack, QStringLiteral("#a05a54"));
    }
}

void ScriptConsole::chooseAndRunFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Run Script"), QString(), QStringLiteral("JavaScript (*.js);;All files (*)"));
    if (!path.isEmpty()) runFile(path);
}

void ScriptConsole::historyStep(int direction)
{
    if (mHistory.isEmpty()) return;
    if (mHistoryPos == mHistory.size()) mPendingInput = mInput->toPlainText();
    mHistoryPos = qBound(0, mHistoryPos + direction, mHistory.size());
    mInput->setPlainText(mHistoryPos == mHistory.size() ? mPendingInput : mHistory.at(mHistoryPos));
    mInput->moveCursor(QTextCursor::End);
}

bool ScriptConsole::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == mInput && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (enter && !(key->modifiers() & Qt::ShiftModifier)) {
            runInput();
            return true;
        }
        // History only while the input is a single line (multi-line edits keep arrows)
        if (key->key() == Qt::Key_Up && !mInput->toPlainText().contains('\n')) {
            historyStep(-1);
            return true;
        }
        if (key->key() == Qt::Key_Down && !mInput->toPlainText().contains('\n')) {
            historyStep(+1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
