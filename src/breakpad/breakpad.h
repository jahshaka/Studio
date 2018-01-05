#include "client\windows\handler\exception_handler.h"
#include "client\windows\sender\crash_report_sender.h"
#include "../constants.h"
#include <string>
#include <qdebug>
#include <QProcess>
#include <QStringList>


bool JahshakaBreakpadCallback(const wchar_t* dump_dir,
	const wchar_t* minidump_id,
	void* context,
	EXCEPTION_POINTERS* exinfo,
	MDRawAssertionInfo* assertion,
	bool succeeded)
{
	qDebug() << "crash captured";
	std::wstring filename = dump_dir;
	filename += L"\\";
	filename += minidump_id;
	filename += L".dmp";

	auto proc = new QProcess();
	QStringList args;
	args << QString::fromWCharArray(filename.c_str());
	args << Constants::CONTENT_VERSION;
	proc->start("crash_handler.exe", args);

	return true;
}

void initializeBreakpad()
{
	std::wstring path = qApp->applicationDirPath().toStdWString();
	auto handler = new google_breakpad::ExceptionHandler(path, /*FilterCallback*/ 0,
		JahshakaBreakpadCallback, /*context*/ 0,
		google_breakpad::ExceptionHandler::HANDLER_ALL);
}