#include <QApplication>
#include "crashreportdialog.h"

int main(int argc, char** argv)
{
	QApplication app(argc, argv);

    app.setApplicationDisplayName("Jahshaka Crash Handler");
    app.setApplicationName("Jahshaka Crash Handler");

	CrashReportDialog crashReporter(nullptr);

	// second parameter is the path to the log file
	if (argc > 1)
		crashReporter.setCrashLogPath(QString(argv[1]));

	// third parameter is the version
	if (argc > 2)
		crashReporter.setVersion(QString(argv[2]));
	crashReporter.show();

	return app.exec();
}
