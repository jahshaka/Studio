/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef BREAKPAD_H
#define BREAKPAD_H

#ifdef USE_BREAKPAD

#if defined(Q_OS_WIN32)
#include "client\windows\handler\exception_handler.h"
#elif defined(Q_OS_LINUX)
#include "client/linux/handler/exception_handler.h"
#endif

#include "../constants.h"
#include <string>
#include <QDebug>
#include <QProcess>
#include <QStringList>

#if defined(Q_OS_WIN32)
bool JahshakaBreakpadCallback(const wchar_t* dump_dir,
	const wchar_t* minidump_id,
	void* context,
	EXCEPTION_POINTERS* exinfo,
	MDRawAssertionInfo* assertion,
	bool succeeded)
{
	std::wstring filename = dump_dir;
	filename += L"\\";
	filename += minidump_id;
	filename += L".dmp";

	auto proc = new QProcess();
	QStringList args;
	args << QString::fromWCharArray(filename.c_str());
	args << Constants::CONTENT_VERSION;
	proc->startDetached("crash_handler.exe", args);
    delete proc;

	return true;
}
#elif defined(Q_OS_LINUX)
bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor,
                                    void* context,
                                    bool succeeded)
{
    auto proc = new QProcess();
    QStringList args;
    args << QString(descriptor.path());
    args << Constants::CONTENT_VERSION;
    QString exePath = QDir::currentPath() + "/crash_handler";
    proc->startDetached(exePath, args);
    delete proc;
}
#endif

void initializeBreakpad()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

#if defined(Q_OS_WIN32)
    auto handler = new google_breakpad::ExceptionHandler(path.toStdWString(), 0,
		JahshakaBreakpadCallback, 0,
		google_breakpad::ExceptionHandler::HANDLER_ALL);
#elif defined(Q_OS_LINUX)
    auto handler = new google_breakpad::ExceptionHandler(google_breakpad::MinidumpDescriptor(path.toStdString()),
        0,
        DumpCallback,
        0,
        true,
        -1);
#endif
}
#endif

#endif // BREAKPAD_H
