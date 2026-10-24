#pragma once

#include <QString>

#include "store.h"

// Writes the pay application for one period as a PDF: the application and
// certificate for payment (G702 format) on the first page, then the
// continuation sheet (G703 format) with every line. Letter paper where the
// language measures in inches, A4 elsewhere. The file appears whole or not
// at all.
bool writePayApplication(const ProjectData &data, int periodId, const QString &path, QString *error);
