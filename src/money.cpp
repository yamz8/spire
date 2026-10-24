#include "money.h"

#include "plumbimport.h"

#include <cmath>

QString formatMoney(Cents cents, const QString &currency, const QLocale &locale) {
    return locale.toCurrencyString(double(cents) / 100.0, currency, 2);
}

QString formatPercent(double percent, const QLocale &locale) {
    // Rounding first keeps a tiny negative remainder from printing as -0.0%.
    const double rounded = std::round(percent * 10) / 10;
    return locale.toString(rounded == 0 ? 0.0 : rounded, 'f', 1) + locale.percent();
}

bool parseMoney(const QString &text, Cents *cents) {
    double value = 0;
    if (!parseNumber(text, &value) || !std::isfinite(value) || std::fabs(value) > 1e13)
        return false;
    *cents = std::llround(value * 100);
    return true;
}
