#include "processor.h"

#include <QDebug>
#include <QtEndian>

Processor::Processor(BlockingQueue *queue, QObject *parent)
    : QObject{parent}, m_queue{queue}
{
    file_vec.reserve(6);
    for(int i = 0; i < 6; i++) {
        QString name = QString("chanel_%1.dat").arg(i);
        QFile* f = new QFile(name);
        f->open(QIODevice::WriteOnly | QIODevice::Append);
        file_vec[i] = f;
    }
}

void Processor::processData()
{
    QByteArray data;

    while (!time_ended) {
        while(true) {
            data = m_queue->Dequeue();

            if (data.isEmpty()) {
                break;
            }
            processPacket(data);
        }
    }
}

void Processor::processPacket(const QByteArray &data)
{
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
            QString line = QString("%1;%2").arg(i_data, q_data);
        }
    }
}
