#include "payapp.h"

#include "billing.h"
#include "money.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <cmath>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("PayApp", text); }

constexpr int Dpi = 150;
const QColor Ink(0x1d, 0x1d, 0x1f);
const QColor Faint(0x6e, 0x6e, 0x73);
const QColor Rule(0xd2, 0xd2, 0xd7);
const QColor Shade(0xf2, 0xf2, 0xf4);

QFont font(qreal points, bool bold = false) {
    QFont f;
    f.setPointSizeF(points);
    f.setBold(bold);
    return f;
}

// The language's short date, but with the full year, as a document that is
// kept for years should have it.
QString dateText(const QDate &date) {
    if (!date.isValid())
        return {};
    QString format = QLocale().dateFormat(QLocale::ShortFormat);
    if (!format.contains("yyyy"))
        format.replace("yy", "yyyy");
    return QLocale().toString(date, format);
}

QString lineLabel(const Line &line) {
    QString text = line.description;
    if (line.quantity > 0)
        text += "  (" + QString("%1 %2").arg(QLocale().toString(line.quantity, 'g', 10), line.unit).trimmed() + ")";
    return text;
}

// Paints in page coordinates, mirrored for right-to-left languages, so the
// layout code reads left to right either way.
struct Canvas {
    QPdfWriter &writer;
    QPainter &painter;
    qreal width = 0;
    qreal height = 0;
    bool rtl = false;

    QRectF place(qreal x, qreal y, qreal w, qreal h) const { return QRectF(rtl ? width - x - w : x, y, w, h); }
    int lead() const { return rtl ? Qt::AlignRight : Qt::AlignLeft; }
    int trail() const { return rtl ? Qt::AlignLeft : Qt::AlignRight; }

    void text(qreal x, qreal y, qreal w, qreal h, const QString &value, const QFont &f, const QColor &color,
              int align) {
        painter.setFont(f);
        painter.setPen(color);
        const QRectF rect = place(x, y, w, h);
        painter.drawText(rect, align | Qt::AlignVCenter | Qt::TextSingleLine,
                         QFontMetricsF(f, &writer).elidedText(value, Qt::ElideRight, rect.width()));
    }
    // Wrapped text from the top; returns its height.
    qreal paragraph(qreal x, qreal y, qreal w, const QString &value, const QFont &f, const QColor &color) {
        painter.setFont(f);
        painter.setPen(color);
        const int flags = lead() | Qt::AlignTop | Qt::TextWordWrap;
        const QRectF bounds = painter.boundingRect(place(x, y, w, 10000), flags, value);
        painter.drawText(place(x, y, w, bounds.height()), flags, value);
        return bounds.height();
    }
    void hline(qreal x, qreal y, qreal w, const QColor &color, qreal thickness = 1) {
        painter.setPen(QPen(color, thickness));
        const QRectF r = place(x, y, w, 0);
        painter.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    void fill(qreal x, qreal y, qreal w, qreal h, const QColor &color) {
        painter.fillRect(place(x, y, w, h), color);
    }
};

void pageLayout(QPdfWriter &writer, bool landscape) {
    const bool letter = QLocale().measurementSystem() != QLocale::MetricSystem;
    writer.setPageLayout(QPageLayout(QPageSize(letter ? QPageSize::Letter : QPageSize::A4),
                                     landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                     QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter));
}

void footer(Canvas &c, int page, int pages, const QString &project) {
    c.text(0, c.height - 28, c.width / 2, 28, project, font(8), Faint, c.lead());
    c.text(c.width / 2, c.height - 28, c.width / 2, 28, tr("Page %1 of %2").arg(QLocale().toString(page), QLocale().toString(pages)),
           font(8), Faint, c.trail());
}

// The application and certificate for payment.
void certificatePage(Canvas &c, const ProjectData &data, const Billing &billing, int pages) {
    const ProjectInfo &info = data.info;
    const BillingSummary &s = billing.summary;
    const Period &period = billing.period;
    auto money = [&](Cents value) { return formatMoney(value, info.currency); };
    const qreal w = c.width;
    qreal y = 0;

    c.text(0, y, w * .7, 48, tr("Application and Certificate for Payment"), font(17, true), Ink, c.lead());
    c.text(w * .7, y, w * .3, 48, tr("Application no. %1").arg(QLocale().toString(period.number)), font(13, true), Ink,
           c.trail());
    y += 56;
    c.hline(0, y, w, Ink, 2.5);
    y += 14;

    // Who and what, in three columns of label and value.
    struct Field { QString label; QString value; };
    const QList<QList<Field>> columns = {
        {{tr("To owner"), info.owner}, {tr("From contractor"), info.contractor}, {tr("Via architect"), info.architect}},
        {{tr("Project"), info.location.isEmpty() ? info.name : info.name + " · " + info.location},
         {tr("Contract for"), info.contractFor},
         {tr("Contract date"), dateText(info.contractDate)}},
        {{tr("Period to"), dateText(period.periodTo)},
         {tr("Application date"), dateText(period.applicationDate)},
         {tr("Contract no."), info.contractNumber}},
    };
    const qreal colWidth = (w - 40) / 3;
    for (int col = 0; col < columns.size(); ++col)
        for (int row = 0; row < columns[col].size(); ++row) {
            const qreal x = col * (colWidth + 20), top = y + row * 64;
            c.text(x, top, colWidth, 22, columns[col][row].label.toUpper(), font(7, true), Faint, c.lead());
            c.text(x, top + 22, colWidth, 32, columns[col][row].value.isEmpty() ? "—" : columns[col][row].value, font(10),
                   Ink, c.lead());
        }
    y += 3 * 64 + 6;
    c.hline(0, y, w, Rule);
    y += 22;

    // The application, on the left.
    const qreal left = w * .56, gap = 36, right = w - left - gap, rightX = left + gap;
    const qreal top = y;
    c.text(0, y, left, 34, tr("Contractor's application for payment"), font(12, true), Ink, c.lead());
    y += 38;
    y += c.paragraph(0, y, left, tr("Application is made for payment, as shown below, in connection with the contract. "
                                   "The continuation sheet is attached."),
                     font(8.5), Faint) + 14;

    const qreal numberW = 34, amountW = left * .25;
    // Labels wrap rather than cut off, in languages with longer words too.
    auto line = [&](const QString &number, const QString &label, const QString &amount, bool bold = false,
                    bool shaded = false, qreal indent = 0) {
        const qreal labelX = numberW + indent + 6, labelW = left - labelX - amountW - 12;
        const int flags = c.lead() | Qt::AlignVCenter | Qt::TextWordWrap;
        const qreal rowH = std::max<qreal>(40, QFontMetricsF(font(9.5, bold), &c.writer)
                                                   .boundingRect(QRectF(0, 0, labelW, 1000), flags, label).height() + 16);
        if (shaded)
            c.fill(0, y, left, rowH, Shade);
        c.text(6, y, numberW, rowH, number, font(9.5, bold), Ink, c.lead());
        c.painter.setFont(font(9.5, bold));
        c.painter.setPen(Ink);
        c.painter.drawText(c.place(labelX, y, labelW, rowH), flags, label);
        if (!amount.isNull())
            c.text(left - amountW - 6, y, amountW, rowH, amount, font(9.5, bold), Ink, c.trail());
        y += rowH;
        c.hline(0, y, left, Rule);
    };
    line("1.", tr("Original contract sum"), money(s.originalContract));
    line("2.", tr("Net change by change orders"), money(s.changeOrders));
    line("3.", tr("Contract sum to date (line 1 ± 2)"), money(s.contractToDate), true);
    line("4.", tr("Total completed and stored to date (column G)"), money(s.completedToDate));
    line("5.", tr("Retainage:"), QString());
    line("", tr("a. %1 of completed work (columns D + E): %2")
                 .arg(formatPercent(period.retainageWork), money(s.completedWork)),
         money(s.retainageWork), false, false, 14);
    line("", tr("b. %1 of stored material (column F): %2")
                 .arg(formatPercent(period.retainageStored), money(s.storedMaterials)),
         money(s.retainageStored), false, false, 14);
    line("", tr("Total retainage (lines 5a + 5b, column I)"), money(s.retainage), false, false, 14);
    line("6.", tr("Total earned less retainage (line 4 − line 5)"), money(s.earnedLessRetainage));
    line("7.", tr("Less previous certificates for payment (line 6 from the prior certificate)"),
         money(s.previousCertificates));
    line("8.", tr("Current payment due"), money(s.currentDue), true, true);
    line("9.", tr("Balance to finish, including retainage (line 3 − line 6)"), money(s.balanceToFinish));
    y += 30;

    // The change order summary.
    const qreal coW = (left - left * .46) / 2;
    auto coRow = [&](const QString &label, const QString &add, const QString &deduct, bool bold) {
        c.text(6, y, left * .46 - 6, 34, label, font(9, bold), Ink, c.lead());
        c.text(left * .46, y, coW - 6, 34, add, font(9, bold), Ink, c.trail());
        c.text(left * .46 + coW, y, coW - 6, 34, deduct, font(9, bold), Ink, c.trail());
        y += 34;
        c.hline(0, y, left, Rule);
    };
    c.fill(0, y, left, 34, Shade);
    coRow(tr("Change order summary"), tr("Additions"), tr("Deductions"), true);
    coRow(tr("Approved in previous months"), money(s.coPreviousAdditions), money(-s.coPreviousDeductions), false);
    coRow(tr("Approved this month"), money(s.coThisAdditions), money(-s.coThisDeductions), false);
    coRow(tr("Totals"), money(s.coPreviousAdditions + s.coThisAdditions),
          money(-(s.coPreviousDeductions + s.coThisDeductions)), true);
    c.text(6, y, left * .46 - 6, 34, tr("Net changes by change order"), font(9, true), Ink, c.lead());
    c.text(left * .46, y, 2 * coW - 6, 34, money(s.changeOrders), font(9, true), Ink, c.trail());

    // The certifications, on the right.
    qreal ry = top;
    auto signature = [&](const QString &first, const QString &second) {
        ry += 50;
        c.hline(rightX, ry, right * .62, Ink);
        c.hline(rightX + right * .7, ry, right * .3, Ink);
        c.text(rightX, ry + 2, right * .62, 24, first, font(7.5), Faint, c.lead());
        c.text(rightX + right * .7, ry + 2, right * .3, 24, second, font(7.5), Faint, c.lead());
        ry += 30;
    };
    c.text(rightX, ry, right, 34, tr("Contractor's certification"), font(12, true), Ink, c.lead());
    ry += 38;
    ry += c.paragraph(rightX, ry, right,
                      tr("The undersigned contractor certifies that, to the best of the contractor's knowledge, "
                         "information, and belief, the work covered by this application has been completed in "
                         "accordance with the contract documents, that all amounts have been paid for work for which "
                         "previous certificates were issued and payments received from the owner, and that the current "
                         "payment shown is now due."),
                      font(8.5), Ink);
    c.text(rightX, ry + 14, right, 26, info.contractor, font(9.5, true), Ink, c.lead());
    ry += 14;
    signature(tr("By"), tr("Date"));
    ry += 34;

    c.text(rightX, ry, right, 34, tr("Architect's certificate for payment"), font(12, true), Ink, c.lead());
    ry += 38;
    ry += c.paragraph(rightX, ry, right,
                      tr("Based on evaluation of the work and the data in this application, the architect certifies to "
                         "the owner that the work has progressed as indicated, that its quality is in accordance with "
                         "the contract documents, and that the contractor is entitled to payment of the amount "
                         "certified."),
                      font(8.5), Ink) + 18;
    c.fill(rightX, ry, right, 44, Shade);
    c.text(rightX + 10, ry, right * .5, 44, tr("Amount certified"), font(9.5, true), Ink, c.lead());
    ry += 44;
    ry += 4 + c.paragraph(rightX, ry + 6, right,
                          tr("Attach an explanation if the amount certified differs from the amount applied for."),
                          font(7.5), Faint);
    c.text(rightX, ry + 14, right, 26, info.architect, font(9.5, true), Ink, c.lead());
    ry += 14;
    signature(tr("By"), tr("Date"));

    footer(c, 1, pages, info.name);
}

// The continuation sheet's columns, as shares of the page width.
struct Column {
    QString letter;
    QString title;
    qreal share;
};

QList<Column> sheetColumns() {
    return {{"A", tr("Item no."), .055},
            {"B", tr("Description of work"), .275},
            {"C", tr("Scheduled value"), .09},
            {"D", tr("From previous application"), .09},
            {"E", tr("This period"), .085},
            {"F", tr("Materials presently stored"), .085},
            {"G", tr("Completed and stored to date"), .095},
            {"", tr("% (G ÷ C)"), .055},
            {"H", tr("Balance to finish"), .09},
            {"I", tr("Retainage"), .08}};
}

constexpr qreal SheetRow = 30;
constexpr qreal SheetHeader = 100;
constexpr qreal SheetTitle = 118;

int rowsPerSheet(qreal height) { return std::max(1, int((height - SheetTitle - SheetHeader - SheetRow - 40) / SheetRow)); }

void continuationPage(Canvas &c, const ProjectData &data, const Billing &billing, int first, int count, bool last,
                      int page, int pages) {
    const ProjectInfo &info = data.info;
    const Period &period = billing.period;
    auto money = [&](Cents value) { return formatMoney(value, info.currency); };
    const qreal w = c.width;
    qreal y = 0;

    c.text(0, y, w * .6, 44, tr("Continuation Sheet"), font(15, true), Ink, c.lead());
    c.text(w * .6, y, w * .4, 44, tr("Application no. %1").arg(QLocale().toString(period.number)), font(12, true), Ink,
           c.trail());
    y += 46;
    c.text(0, y, w * .6, 26, info.name, font(9), Faint, c.lead());
    c.text(w * .4, y, w * .6, 26,
           tr("Application date %1 · Period to %2 · Retainage %3 work, %4 stored")
               .arg(dateText(period.applicationDate), dateText(period.periodTo), formatPercent(period.retainageWork),
                    formatPercent(period.retainageStored)),
           font(9), Faint, c.trail());
    y = SheetTitle - 14;
    c.hline(0, y, w, Ink, 2.5);
    y = SheetTitle;

    const QList<Column> columns = sheetColumns();
    QList<qreal> xs;
    qreal x = 0;
    for (const auto &column : columns) {
        xs << x;
        x += column.share * w;
    }
    auto cell = [&](int i, qreal top, qreal height, const QString &value, const QFont &f, const QColor &color) {
        const int align = i <= 1 ? c.lead() : c.trail();
        c.text(xs[i] + 5, top, columns[i].share * w - 10, height, value, f, color, align);
    };

    // Headings: the column letter above a wrapped title.
    for (int i = 0; i < columns.size(); ++i) {
        const int align = i <= 1 ? c.lead() : c.trail();
        c.text(xs[i] + 5, y, columns[i].share * w - 10, 26, columns[i].letter, font(8, true), Faint, align);
        c.painter.setFont(font(7.5, true));
        c.painter.setPen(Ink);
        c.painter.drawText(c.place(xs[i] + 5, y + 26, columns[i].share * w - 10, SheetHeader - 30),
                           align | Qt::AlignBottom | Qt::TextWordWrap, columns[i].title);
    }
    y += SheetHeader;
    c.hline(0, y, w, Ink, 1.5);

    for (int r = first; r < first + count && r < billing.rows.size(); ++r) {
        const BillingRow &row = billing.rows[r];
        const QString number = !row.line.code.isEmpty()   ? row.line.code
                               : row.line.isChangeOrder() ? tr("CO %1").arg(QLocale().toString(row.line.coNumber))
                                                          : QLocale().toString(r + 1);
        const QStringList values = {number,
                                    lineLabel(row.line),
                                    money(row.scheduled),
                                    money(row.previous),
                                    money(row.thisPeriod),
                                    money(row.stored),
                                    money(row.toDate),
                                    formatPercent(row.percentToDate),
                                    money(row.balance),
                                    money(row.retainage)};
        for (int i = 0; i < values.size(); ++i)
            cell(i, y, SheetRow, values[i], font(8), Ink);
        y += SheetRow;
        c.hline(0, y, w, Rule);
    }
    if (billing.rows.isEmpty()) {
        cell(1, y, SheetRow, tr("No lines on the schedule of values yet."), font(8), Faint);
        y += SheetRow;
    }

    if (last) {
        const BillingSummary &s = billing.summary;
        c.fill(0, y, w, SheetRow + 4, Shade);
        c.hline(0, y, w, Ink, 1.5);
        const QStringList totals = {QString(),
                                    tr("Grand total"),
                                    money(s.scheduledTotal),
                                    money(s.previousTotal),
                                    money(s.thisPeriodTotal),
                                    money(s.storedMaterials),
                                    money(s.completedToDate),
                                    formatPercent(s.percentComplete),
                                    money(s.balanceTotal),
                                    money(s.retainage)};
        for (int i = 0; i < totals.size(); ++i)
            cell(i, y + 2, SheetRow, totals[i], font(8, true), Ink);
    }
    footer(c, page, pages, info.name);
}
}

bool writePayApplication(const ProjectData &data, int periodId, const QString &path, QString *error) {
    if (!data.period(periodId)) {
        *error = tr("There is no such application.");
        return false;
    }
    const Billing billing = computeBilling(data, periodId);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    {
        QPdfWriter writer(&file);
        writer.setTitle(tr("%1 pay application %2").arg(data.info.name, QLocale().toString(billing.period.number)));
        writer.setCreator("Spire");
        writer.setResolution(Dpi);

        // The sheet's page count is known before anything is drawn, so every
        // page can say "of N".
        pageLayout(writer, true);
        const QSizeF landscape = writer.pageLayout().paintRectPixels(Dpi).size();
        const int perSheet = rowsPerSheet(landscape.height());
        const int sheets = std::max(1, int((billing.rows.size() + perSheet - 1) / perSheet));
        const int pages = 1 + sheets;

        pageLayout(writer, false);
        QPainter painter(&writer);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool rtl = QLocale().textDirection() == Qt::RightToLeft;
        const QSizeF portrait = writer.pageLayout().paintRectPixels(Dpi).size();
        Canvas certificate{writer, painter, portrait.width(), portrait.height(), rtl};
        certificatePage(certificate, data, billing, pages);

        for (int sheet = 0; sheet < sheets; ++sheet) {
            pageLayout(writer, true);
            writer.newPage();
            Canvas canvas{writer, painter, landscape.width(), landscape.height(), rtl};
            continuationPage(canvas, data, billing, sheet * perSheet, perSheet, sheet == sheets - 1, sheet + 2, pages);
        }
        if (!painter.end()) {
            file.cancelWriting();
            *error = tr("Could not write %1.").arg(path);
            return false;
        }
    }
    if (!file.commit()) {
        *error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
