#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "blockingqueue.h"
#include "csvwriter.h"

#include <QObject>
#include <QFile>

class Processor : public QObject
{
    Q_OBJECT
public:
    explicit Processor(BlockingQueue *queue, QObject *parent = nullptr);
public slots:
    void processData();
    void onTimerFinished();
    void onCsvWritingFinished();

signals:
    void processorFinished();

private:
    BlockingQueue *m_queue;
    bool finished = false;
    CsvWriter *m_writer;
    QThread *writer_thread;
    QVector<CsvBuffer *> m_buffers;

    void processPacket(const QByteArray &data);
    void writeToChanel(int file_number, const QByteArray &data);
};

#endif // PROCESSOR_H
