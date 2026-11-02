#include "plumbimport.h"

#include "billing.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Import", text); }

// ItemName is Plumb's Item column: the item's name, or its code when the
// file also has a description.
enum Field { Code, Description, ItemName, Sheet, Quantity, Unit, UnitPrice, Amount, Installed };

// What Plumb writes in its export, in each language it speaks: the Item,
// Sheet, Quantity and Unit headings, the label of an item's total row, and
// the unit of a sheet that has no scale.
const char *const plumb_item[] = {
    "Articol", "Artigo", "Band", "Concepto", "Hạng mục", "Item", "Kalem", "Kipengee", "Kirje", "Liður", "Mír",
    "Nimike", "Partida", "Položka", "Position", "Post", "Postavka", "Poste", "Pozicija", "Pozycja",
    "Pozīcija", "Stavka", "Tétel", "Voce", "Άρθρο", "Позиция", "Позиція", "Ставка", "סעיף", "آئٹم", "آیتم",
    "البند", "आइटम", "আইটেম", "உருப்படி", "අයිතමය", "รายการ", "項目", "项目", "항목",
};
const char *const plumb_sheet[] = {
    "Ark", "Arkusz", "Bileog", "Blad", "Blatt", "Blað", "Folha", "Full", "Helaian", "Hoja", "Laha", "Lap",
    "Lapa", "Lapas", "Leht", "Lehti", "Lembar", "List", "Pafta", "Planche", "Planșă", "Prancha", "Sheet",
    "Tavola", "Tờ", "Varaq", "Φύλλο", "Аркуш", "Лист", "גיליון", "اللوحة", "برگه", "شیٹ", "शीट", "শিট",
    "தாள்", "පත්‍රය", "แผ่นแบบ", "シート", "图纸", "圖面", "시트",
};
const char *const plumb_quantity[] = {
    "Apjoms", "Cainníocht", "Cantidad", "Cantitate", "Dami", "Hajm", "Hoeveelheid", "Ilość", "Khối lượng",
    "Kiasi", "Kiekis", "Kogus", "Količina", "Kuantiti", "Magn", "Mengde", "Menge", "Mennyiség", "Miktar",
    "Mängd", "Määrä", "Mængde", "Quantidade", "Quantitat", "Quantity", "Quantità", "Quantité", "Volume",
    "Výmera", "Výměra", "Ποσότητα", "Количество", "Количина", "Обсяг", "Объём", "כמות", "الكمية", "مقدار",
    "मात्रा", "পরিমাণ", "அளவு", "ප්‍රමාණය", "ปริมาณ", "工程量", "数量", "數量", "수량",
};
const char *const plumb_unit[] = {
    "Aonad", "Birim", "Birlik", "Eenheid", "Egység", "Einheit", "Eining", "Enhed", "Enhet", "Enota", "Jed.",
    "Jedn.", "Kizio", "MJ", "Mērvienība", "Satuan", "Unidad", "Unidade", "Unit", "Unitat", "Unitate", "Unità",
    "Unité", "Vienetas", "Yksikkö", "Ühik", "Đơn vị", "Μονάδα", "Јед.", "Ед. изм.", "Мярка", "Од.", "יחידה",
    "الوحدة", "اکائی", "واحد", "इकाई", "একক", "அலகு", "ඒකකය", "หน่วย", "单位", "単位", "單位", "단위",
};
const char *const plumb_allSheets[] = {
    "All sheets", "Alla blad", "Alle Blätter", "Alle ark", "Alle bladen", "Barcha varaqlar", "Gach bileog",
    "Kaikki lehdet", "Kõik lehed", "Laha zote", "Lahat ng sheet", "Minden lap", "Mọi tờ", "Semua helaian",
    "Semua lembar", "Svi listovi", "Toate planșele", "Todas as folhas", "Todas as pranchas",
    "Todas las hojas", "Tots els fulls", "Toutes les planches", "Tutte le tavole", "Tüm paftalar",
    "Visas lapas", "Visi lapai", "Vsi listi", "Všechny listy", "Všetky listy", "Wszystkie arkusze",
    "Öll blöð", "Όλα τα φύλλα", "Все листы", "Всички листове", "Сви листови", "Усі аркуші", "כל הגיליונות",
    "تمام شیٹس", "كل اللوحات", "همه برگه‌ها", "सभी शीट", "সব শিট", "எல்லாத் தாள்களும்", "සියලු පත්‍ර",
    "ทุกแผ่นแบบ", "全シート", "所有图纸", "所有圖面", "모든 시트",
};
const char *const plumb_noScale[] = {
    "Bez mierky", "Bez měřítka", "Brak skali", "Chưa có tỷ lệ", "Ei mittakaavaa", "Enginn mælikvarði",
    "Fără scară", "Gan scála", "Geen schaal", "Hakuna skeli", "Ingen målestok", "Ingen målestokk",
    "Ingen skala", "Kein Maßstab", "Masshtab yoʻq", "Mõõtkava puudub", "Nav mēroga", "Nema mjerila",
    "Ni merila", "Nincs lépték", "No scale", "Nėra mastelio", "Pas d'échelle", "Sem escala", "Sense escala",
    "Senza scala", "Sin escala", "Tanpa skala", "Tiada skala", "Walang scale", "Ölçek yok", "Χωρίς κλίμακα",
    "Нема размере", "Немає масштабу", "Нет масштаба", "Няма мащаб", "אין קנה מידה", "اسکیل نہیں",
    "بدون مقیاس", "بلا مقياس", "स्केल नहीं", "স্কেল নেই", "அளவுகோல் இல்லை", "පරිමාණයක් නැත", "ไม่มีมาตราส่วน",
    "无比例", "無比例", "縮尺なし", "축척 없음",
};

QString normalize(const QString &heading);

template <size_t N> QSet<QString> normalized(const char *const (&words)[N]) {
    QSet<QString> set;
    for (const char *w : words)
        set.insert(normalize(QString::fromUtf8(w)));
    return set;
}

// Headings Plumb writes, and the ones people use when they tidy an export
// in a spreadsheet, normalized to lowercase letters and digits only.
const QHash<QString, Field> &headings() {
    static const QHash<QString, Field> map = [] {
        QHash<QString, Field> m;
        for (const char *h : {"code", "itemno", "itemnumber", "costcode", "id", "no", "number", "ref"})
            m.insert(h, Code);
        for (const char *h : {"description", "name", "element", "itemdescription", "workdescription", "label",
                              "assembly", "condition"})
            m.insert(h, Description);
        for (const char *h : {"quantity", "qty", "totalquantity", "totalqty", "takeoff", "takeoffquantity", "measured",
                              "contractquantity", "estimatedquantity"})
            m.insert(h, Quantity);
        for (const char *h : {"unit", "units", "uom", "unitofmeasure", "measure"})
            m.insert(h, Unit);
        for (const char *h : {"unitprice", "rate", "unitcost", "price", "unitrate", "priceperunit"})
            m.insert(h, UnitPrice);
        for (const char *h : {"amount", "total", "value", "scheduledvalue", "extended", "extendedprice", "cost",
                              "totalcost", "totalprice"})
            m.insert(h, Amount);
        for (const char *h : {"installed", "complete", "completed", "installedquantity", "installedqty",
                              "installedtodate", "completedquantity", "completedqty", "quantitytodate", "todate",
                              "qtyinstalled", "quantityinstalled", "qtycomplete", "quantitycomplete"})
            m.insert(h, Installed);
        auto add = [&m](const QSet<QString> &words, Field field) {
            for (const QString &w : words)
                if (!w.isEmpty() && !m.contains(w))
                    m.insert(w, field);
        };
        add(normalized(plumb_item), ItemName);
        add(normalized(plumb_sheet), Sheet);
        add(normalized(plumb_quantity), Quantity);
        add(normalized(plumb_unit), Unit);
        return m;
    }();
    return map;
}

QString normalize(const QString &heading) {
    QString key;
    for (const QChar c : heading.toLower())
        if (c.isLetterOrNumber())
            key += c;
    return key;
}

QChar detectDelimiter(const QString &line) {
    int counts[3] = {0, 0, 0};
    const QChar candidates[3] = {',', ';', '\t'};
    bool quoted = false;
    for (const QChar c : line) {
        if (c == '"')
            quoted = !quoted;
        else if (!quoted)
            for (int i = 0; i < 3; ++i)
                if (c == candidates[i])
                    counts[i]++;
    }
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (counts[i] > counts[best])
            best = i;
    return candidates[best];
}

// Splits CSV text into records, honoring quotes, doubled quotes, and line
// breaks inside quoted fields. Each record remembers its first line number.
QList<QPair<int, QStringList>> records(const QString &text, QChar delimiter) {
    QList<QPair<int, QStringList>> out;
    QStringList fields;
    QString field;
    bool quoted = false, any = false;
    int line = 1, start = 1;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                if (c == '\n')
                    ++line;
                field += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            any = true;
        } else if (c == delimiter) {
            fields << field;
            field.clear();
            any = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            if (any || !field.isEmpty()) {
                fields << field;
                out.append({start, fields});
            }
            fields.clear();
            field.clear();
            any = false;
            start = ++line;
        } else {
            field += c;
            any = any || !c.isSpace();
        }
    }
    if (any || !field.isEmpty()) {
        fields << field;
        out.append({start, fields});
    }
    return out;
}

// Plumb puts an apostrophe before text starting with = + - or @, so a
// spreadsheet doesn't take it for a formula.
QString unescaped(const QString &text) {
    if (text.size() > 1 && text[0] == '\'' && QString("=+-@").contains(text[1]))
        return text.mid(1);
    return text;
}

Cents toCents(double value) { return std::llround(value * 100); }

void addNote(ImportChange &change, const QString &note) {
    change.note = change.note.isEmpty() ? note : change.note + " " + note;
}

QString key(const TakeoffRow &row) {
    return (row.code.isEmpty() ? "d:" + row.description.simplified().toLower() : "c:" + row.code.trimmed().toLower()) +
           "|" + row.unit.trimmed().toLower();
}
}

bool parseNumber(const QString &input, double *value, Decimals decimals) {
    QString text = input.trimmed();
    bool negative = false;
    if (text.startsWith('(') && text.endsWith(')')) {
        negative = true;
        text = text.mid(1, text.size() - 2);
    }
    // Currency symbols, spaces (including the narrow no-break ones French
    // uses between thousands), and stray percent or unit marks go.
    QString digits;
    for (const QChar c : text)
        if (c.isDigit() || c == '.' || c == ',' || c == '-')
            digits += c;
    if (digits.startsWith('-')) {
        negative = !negative;
        digits.remove(0, 1);
    }
    if (digits.isEmpty() || digits.contains('-'))
        return false;
    const int lastDot = digits.lastIndexOf('.'), lastComma = digits.lastIndexOf(',');
    if (decimals != Decimals::Auto) {
        digits.remove(decimals == Decimals::Comma ? '.' : ',');
        digits.replace(',', '.');
    } else if (lastDot >= 0 && lastComma >= 0) {
        // Both appear: the later one separates the decimals.
        const QChar thousands = lastDot > lastComma ? ',' : '.';
        digits.remove(thousands);
        digits.replace(',', '.');
    } else if (lastComma >= 0) {
        // A single comma followed by three digits groups thousands
        // (1,250); otherwise it is a decimal comma (12,5).
        const bool grouping = digits.count(',') > 1 || digits.size() - lastComma - 1 == 3;
        if (grouping)
            digits.remove(',');
        else
            digits.replace(',', '.');
    } else if (digits.count('.') > 1) {
        digits.remove('.');
    }
    bool ok = false;
    const double number = digits.toDouble(&ok);
    if (!ok)
        return false;
    *value = negative ? -number : number;
    return true;
}

Takeoff parseTakeoff(const QByteArray &csv) {
    Takeoff takeoff;
    QString text = QString::fromUtf8(csv);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    // The heading row is the first one that names a description or code
    // and a quantity; anything above it (a title, a comment) is skipped.
    QChar delimiter = ',';
    for (const auto &line : text.split('\n'))
        if (!line.trimmed().isEmpty() && !line.trimmed().startsWith('#')) {
            delimiter = detectDelimiter(line);
            break;
        }
    const auto all = records(text, delimiter);
    QHash<Field, int> columns;
    int first = -1;
    for (int r = 0; r < all.size() && first < 0; ++r) {
        QHash<Field, int> found;
        for (int c = 0; c < all[r].second.size(); ++c) {
            const QString heading = normalize(all[r].second[c]);
            if (headings().contains(heading) && !found.contains(headings().value(heading)))
                found.insert(headings().value(heading), c);
        }
        if (found.contains(ItemName)) {
            if (!found.contains(Description))
                found.insert(Description, found.value(ItemName));
            else if (!found.contains(Code))
                found.insert(Code, found.value(ItemName));
        }
        if ((found.contains(Description) || found.contains(Code)) && (found.contains(Quantity) || found.contains(Amount))) {
            columns = found;
            first = r + 1;
        }
    }
    if (first < 0) {
        takeoff.error = tr("No heading row found. A Plumb export needs a Description (or Code) column and a "
                           "Quantity (or Amount) column.");
        return takeoff;
    }
    takeoff.hasInstalled = columns.contains(Installed);

    // A file writes all its numbers one way. One number that can only be
    // read with a decimal comma (12,5 or 1.234,50) settles it for numbers
    // like 1.240 that could be read either way; likewise for a point.
    Decimals decimals = Decimals::Auto;
    {
        static const QRegularExpression comma(R"(^-?\d{1,3}(\.\d{3})+,\d+$|^-?\d+,(\d{1,2}|\d{4,})$)");
        static const QRegularExpression point(R"(^-?\d{1,3}(,\d{3})+\.\d+$|^-?\d+\.(\d{1,2}|\d{4,})$)");
        int commas = 0, points = 0;
        for (int r = first; r < all.size(); ++r)
            for (const Field f : {Quantity, UnitPrice, Amount, Installed})
                if (columns.contains(f)) {
                    QString cell = all[r].second.value(columns.value(f)).trimmed();
                    cell.remove(QRegularExpression(R"([^\d.,-])"));
                    commas += comma.match(cell).hasMatch();
                    points += point.match(cell).hasMatch();
                }
        if (commas > points)
            decimals = Decimals::Comma;
        else if (points > commas)
            decimals = Decimals::Point;
    }

    // Plumb writes a row per item and sheet, then the item's total over all
    // sheets. The total is used when there is one; otherwise sheets add up.
    static const QSet<QString> totalLabels = normalized(plumb_allSheets);
    static const QSet<QString> noScale = normalized(plumb_noScale);
    QList<QPair<TakeoffRow, bool>> read;
    for (int r = first; r < all.size(); ++r) {
        const auto &[lineNumber, fields] = all[r];
        auto cell = [&](Field f) { return columns.contains(f) ? fields.value(columns.value(f)).trimmed() : QString(); };
        if (fields.join("").trimmed().isEmpty() || fields.value(0).trimmed().startsWith('#'))
            continue;
        TakeoffRow row;
        row.sourceLine = lineNumber;
        row.code = unescaped(cell(Code));
        row.description = unescaped(cell(Description)).simplified();
        row.unit = cell(Unit);
        const QString sheet = unescaped(cell(Sheet));
        const bool total = !sheet.isEmpty() && totalLabels.contains(normalize(sheet));
        // A totals row at the bottom of the export is not an item.
        const QString label = (row.description.isEmpty() ? row.code : row.description).toLower();
        if (label == "total" || label == "totals" || label == "grand total" || label == "subtotal")
            continue;
        if (row.code.isEmpty() && row.description.isEmpty()) {
            takeoff.warnings << tr("Line %1 has no code or description.").arg(lineNumber);
            continue;
        }
        if (cell(Quantity).isEmpty() && noScale.contains(normalize(row.unit))) {
            takeoff.warnings << tr("Line %1: %2 on sheet %3 is not counted, because the sheet has no scale.")
                                    .arg(lineNumber)
                                    .arg(row.description.isEmpty() ? row.code : row.description, sheet);
            continue;
        }
        double number = 0;
        const QString quantity = cell(Quantity);
        if (!quantity.isEmpty() && !parseNumber(quantity, &row.quantity, decimals)) {
            takeoff.warnings << tr("Line %1: “%2” is not a quantity.").arg(lineNumber).arg(quantity);
            continue;
        }
        if (!cell(UnitPrice).isEmpty()) {
            if (!parseNumber(cell(UnitPrice), &number, decimals)) {
                takeoff.warnings << tr("Line %1: “%2” is not a unit price.").arg(lineNumber).arg(cell(UnitPrice));
                continue;
            }
            row.unitPrice = toCents(number);
            row.hasUnitPrice = true;
        }
        if (!cell(Amount).isEmpty()) {
            if (!parseNumber(cell(Amount), &number, decimals)) {
                takeoff.warnings << tr("Line %1: “%2” is not an amount.").arg(lineNumber).arg(cell(Amount));
                continue;
            }
            row.amount = toCents(number);
            row.hasAmount = true;
        }
        if (!cell(Installed).isEmpty()) {
            if (!parseNumber(cell(Installed), &row.installed, decimals)) {
                takeoff.warnings << tr("Line %1: “%2” is not an installed quantity.").arg(lineNumber).arg(cell(Installed));
                continue;
            }
            row.hasInstalled = true;
        }
        if (quantity.isEmpty() && !row.hasAmount) {
            takeoff.warnings << tr("Line %1 has no quantity.").arg(lineNumber);
            continue;
        }

        read.append({row, total});
    }

    QSet<QString> totaled;
    for (const auto &[row, total] : read)
        if (total)
            totaled.insert(key(row));
    QHash<QString, int> merged;
    for (const auto &[row, total] : read) {
        const QString k = key(row);
        if (totaled.contains(k) && !total)
            continue;
        if (merged.contains(k)) {
            TakeoffRow &into = takeoff.rows[merged.value(k)];
            into.quantity += row.quantity;
            into.installed += row.installed;
            into.hasInstalled = into.hasInstalled || row.hasInstalled;
            into.amount += row.amount;
            into.hasAmount = into.hasAmount || row.hasAmount;
            if (!into.hasUnitPrice && row.hasUnitPrice) {
                into.unitPrice = row.unitPrice;
                into.hasUnitPrice = true;
            }
            if (into.description.isEmpty())
                into.description = row.description;
        } else {
            merged.insert(k, takeoff.rows.size());
            takeoff.rows << row;
        }
    }
    if (takeoff.rows.isEmpty() && takeoff.error.isEmpty())
        takeoff.error = tr("The file has headings but no items.");
    return takeoff;
}

Takeoff readTakeoff(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Takeoff takeoff;
        takeoff.error = tr("Could not read %1: %2").arg(path, file.errorString());
        return takeoff;
    }
    return parseTakeoff(file.readAll());
}

ImportPlan planImport(const ProjectData &data, const Takeoff &takeoff, int periodId) {
    ImportPlan plan;
    const Period *period = data.period(periodId);
    const bool canBill = period && !period->submitted;
    const bool locked = data.sovLocked();

    QHash<QString, const Line *> byCode, byDescription;
    for (const auto &line : data.lines) {
        if (period && !line.billableIn(period->number))
            continue;
        if (!line.code.trimmed().isEmpty())
            byCode.insert(line.code.trimmed().toLower(), &line);
        if (!line.description.trimmed().isEmpty())
            byDescription.insert(line.description.simplified().toLower(), &line);
    }

    for (const auto &row : takeoff.rows) {
        ImportChange change;
        change.row = row;
        const Line *match = nullptr;
        if (!row.code.isEmpty())
            match = byCode.value(row.code.trimmed().toLower());
        if (!match && !row.description.isEmpty())
            match = byDescription.value(row.description.toLower());

        // The amount the takeoff prices the item at, if it does. Plumb only
        // measures, so its quantities are priced at the line's own unit
        // price when it has one.
        const Cents unitPrice = row.hasUnitPrice ? row.unitPrice : match ? match->unitPrice : 0;
        const bool priced = unitPrice > 0 || row.hasAmount;
        const Cents amount = unitPrice > 0 ? std::llround(row.quantity * double(unitPrice)) : row.amount;

        if (match) {
            change.action = ImportChange::Update;
            change.line = *match;
            const bool fixed = match->isChangeOrder() ? data.billedOnSubmitted(match->id) : locked;
            const bool differs = match->quantity != row.quantity || (!row.unit.isEmpty() && match->unit != row.unit) ||
                                 (priced && match->scheduled != amount) ||
                                 (row.hasUnitPrice && match->unitPrice != row.unitPrice);
            if (differs && fixed) {
                change.note = tr("Schedule of values is locked; amounts kept.");
            } else if (differs) {
                change.line.quantity = row.quantity;
                if (!row.unit.isEmpty())
                    change.line.unit = row.unit;
                if (row.hasUnitPrice)
                    change.line.unitPrice = row.unitPrice;
                if (priced)
                    change.line.scheduled = amount;
                change.scheduleChanged = true;
            }
        } else if (locked) {
            change.action = ImportChange::Skip;
            change.note = tr("Not on the schedule of values, which is locked. Add it as a change order.");
        } else {
            change.action = ImportChange::Add;
            change.line.code = row.code;
            change.line.description = row.description.isEmpty() ? row.code : row.description;
            change.line.quantity = row.quantity;
            change.line.unit = row.unit;
            change.line.unitPrice = unitPrice;
            change.line.scheduled = amount;
            change.scheduleChanged = true;
            if (!priced)
                change.note = tr("No price in the takeoff; set the scheduled value.");
        }

        if (change.action != ImportChange::Skip && row.hasInstalled) {
            if (!period) {
                addNote(change, tr("Installed quantity ignored: start an application to bill it."));
            } else if (!canBill) {
                addNote(change, tr("Installed quantity ignored: this application was submitted."));
            } else if (row.quantity <= 0 && change.line.quantity <= 0) {
                addNote(change, tr("Installed quantity ignored: no total quantity to measure against."));
            } else {
                // Measured against the takeoff's own total, which is the
                // line's too unless the schedule is locked.
                double share = row.installed / (row.quantity > 0 ? row.quantity : change.line.quantity);
                if (share > 1) {
                    addNote(change, tr("Installed is more than the quantity; billed at 100%."));
                    share = 1;
                }
                share = std::max(0.0, share);
                change.billing = true;
                change.percent = share * 100;
                change.completed = percentOf(change.line.scheduled, change.percent);
            }
        }

        if (change.action == ImportChange::Update && !change.scheduleChanged && !change.billing)
            change.action = ImportChange::Same;
        switch (change.action) {
        case ImportChange::Same: plan.unchanged++; break;
        case ImportChange::Update: plan.updates++; break;
        case ImportChange::Add: plan.additions++; break;
        case ImportChange::Skip: plan.skipped++; break;
        }
        if (change.billing)
            plan.billed++;
        plan.changes << change;
    }
    return plan;
}
