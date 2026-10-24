#pragma once

#include <QStringList>

// Commands that work on a project file without a window, for scripts and
// agents: spire new, info, lines, bill, import, periods, export, and more.
bool isCliCommand(const char *argument);
QString cliSummary();
int runCli(const QStringList &arguments);
