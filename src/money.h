#pragma once

#include <QLocale>
#include <QString>

#include "store.h"

// Amounts shown and printed in the language's own separators, with the
// project's currency symbol: $1,234.50, or 1.234,50 $ in German.
QString formatMoney(Cents cents, const QString &currency, const QLocale &locale = QLocale());
// A percentage with one decimal, as the language writes it (12.5%, 12,5 %).
QString formatPercent(double percent, const QLocale &locale = QLocale());
// Reads an amount typed by hand: 1234.5, 1,234.50, $ 1.234,50, (250) or -250.
bool parseMoney(const QString &text, Cents *cents);
