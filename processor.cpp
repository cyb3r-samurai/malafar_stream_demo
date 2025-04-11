#include "processor.h"

#include <QDebug>
#include <QtEndian>
#include <QThread>

Processor::Processor(BlockingQueue *queue, QObject *parent)
    : QObject{parent}, m_queue{queue}
{
    qDebug() << "Processor created";
    for (int i = 0; i < 6; ++i) {
        m_buffers.append(new CsvBuffer);
    }

    writer_thread = new QThread();
    m_writer = new CsvWriter(&m_buffers);

    connect(writer_thread, &QThread::started,
            m_writer, &CsvWriter::flushBuffers);
    connect(m_writer, &CsvWriter::writingFinished,
            writer_thread, &QThread::quit);
    connect(m_writer, &CsvWriter::writingFinished,
            m_writer, &CsvWriter::deleteLater);
    connect(writer_thread, &QThread::finished,
            writer_thread, &QThread::deleteLater);

    connect(m_writer,&CsvWriter::writingFinished,
            this, &Processor::onCsvWritingFinished);

    m_writer->moveToThread(writer_thread);
}

void Processor::processData()
{
    qDebug() << "ProcessData started";
    writer_thread->start();
    qDebug() << QThread::currentThreadId();
    QByteArray data;
    while (!finished) {

        while(true) {
            data = m_queue->Dequeue();
            if (data.isEmpty()) {
                break;
            }
//            qDebug() << "message get" ;
            processPacket(data);
        }
    }
}

void Processor::onTimerFinished()
{
    qDebug() << "Timer finished";
    finished = true;
}

void Processor::processPacket(const QByteArray &data)
{
  //  qDebug() << "processing started";
    const int chanel_count = 6;
    const int report_size = 4;

    const char* char_data = data.constData();
    int total_reports = data.size() / report_size;
    if (total_reports % chanel_count != 0) {
        qWarning() << " missing data";
    }

    int total_sets = total_reports / chanel_count;

    for (int i = 0; i < total_sets; ++i) {
        for (int ch = 0; ch < chanel_count; ++ch) {
            int offset = (i * chanel_count + ch) * report_size;
            const int16_t* report = reinterpret_cast<const int16_t *>(char_data+offset);
            int16_t i_data = qFromLittleEndian<qint16>(report[0]);
            int16_t q_data = qFromLittleEndian<qint16>(report[1]);
            QByteArray byte_line = QByteArray::number(i_data) + ";"
                                   + QByteArray::number(q_data);
            m_buffers[ch]->append(byte_line);
        }
    }
}

void Processor::onCsvWritingFinished()
{
    qDebug() <<" CsvWritingFinished";
    emit processorFinished();
}
