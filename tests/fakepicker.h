#pragma once

#include <QStringList>

#include "filepicker.h"

// Answers every dialog with the files it was given, as the portal would.
class FakePicker : public FilePicker {
public:
    QStringList answers;
    QString lastPurpose;
    void open(const QString &purpose, const QString &, const QList<FileFilter> &, bool, const QString &) override {
        lastPurpose = purpose;
        QList<QUrl> urls;
        for (const auto &answer : answers)
            urls << QUrl::fromLocalFile(answer);
        emit selected(purpose, urls);
    }
    void save(const QString &purpose, const QString &, const QString &, const FileFilter &) override {
        lastPurpose = purpose;
        emit selected(purpose, {QUrl::fromLocalFile(answers.value(0))});
    }
};
