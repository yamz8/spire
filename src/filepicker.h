#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>

struct FileFilter {
    QString name;
    QStringList patterns;
    QStringList mimeTypes;
};

// Open and save dialogs, behind an interface so tests can answer them without
// a desktop. A request carries a purpose, which comes back with the answer so
// the caller knows what the chosen files are for.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void open(const QString &purpose, const QString &title, const QList<FileFilter> &filters,
                      bool multiple, const QString &folder) = 0;
    virtual void save(const QString &purpose, const QString &title, const QString &suggestedPath,
                      const FileFilter &filter) = 0;

signals:
    void selected(const QString &purpose, const QList<QUrl> &urls);
    void failed(const QString &message);
};
