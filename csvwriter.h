#ifndef CSVWRITER_H
#define CSVWRITER_H

#include <QObject>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QDebug>

struct CsvBuffer
{
    QVector<QString> lines;
    QMutex mutex;

    void append(const QString& line);
    QVector<QString> takeAll();
};

class CsvWriter : public QObject
{
    Q_OBJECT
public:
    explicit CsvWriter(QVector<CsvBuffer *>* buffers,
                       QObject *parrent = nullptr);

public slots:
    void flushBuffers();
private:
    QVector<CsvBuffer *> * m_buffers;
    QVector<QFile *> m_files;
    QVector<QTextStream *> m_streams;
};

#endif // CSVWRITER_H
