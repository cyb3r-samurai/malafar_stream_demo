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
    void on_time_ending();

signals:

private:
    BlockingQueue *m_queue;
    QVector<QFile *> file_vec;
    bool time_ended = false;
    QThread *writer_thread;

    void processPacket(const QByteArray &data);
    void writeToChanel(int file_number, const QByteArray &data);
};

#endif // PROCESSOR_H
