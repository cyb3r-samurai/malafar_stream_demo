#ifndef CSVWRITER_H
#define CSVWRITER_H

#include <QObject>
#include <QMutex>
#include <QFile>
#include <QTextStream>
#include <QDebug>

struct CsvBuffer
{
    QVector<QByteArray> lines;
    QMutex mutex;

    void append(const QByteArray& line);
    QVector<QByteArray> takeAll();
};

class CsvWriter : public QObject
{
    Q_OBJECT
public:
    explicit CsvWriter(QVector<CsvBuffer *>* buffers,
                       QObject *parrent = nullptr);

signals:
    void writingFinished();

public slots:
    void flushBuffers();
    void onTimerFineshed();
private:
    QVector<CsvBuffer *> * m_buffers;
    QVector<QFile *> m_files;
    QVector<QTextStream *> m_streams;

    bool finished;

    void closeFiles();
};

#endif // CSVWRITER_H
