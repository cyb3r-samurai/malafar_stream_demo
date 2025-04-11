#include "csvwriter.h"
#include <QThread>

CsvWriter::CsvWriter(QVector<CsvBuffer *> *buffers, QObject * parent)
    : QObject(parent), m_buffers{buffers}
{
    qDebug() << "Writer created";
    for (int i = 0; i < 6; ++i) {
        QString file_name = QString("report_%1.csv").arg(i+1);
        QFile* f = new QFile(file_name);
        if(!f->open(QIODevice::WriteOnly |
                    QIODevice::Append    |
                    QIODevice::Text       ))
        {
            qDebug() << " File not open";
        }
        m_files.append(f);
        m_streams.append(new QTextStream(f));
    }
}

// Пишет отчеты из буффер в csv файлы.
// Запись останавливается после того как закончится таймер и оставшиеся данные
// в буффере будут записаны.
void CsvWriter::flushBuffers()
{
    qDebug() << "flushing started";
    qDebug() << "writer thread ID" << QThread::currentThreadId();
    while(!finished)
    {
        while (true) {
            int empty_buffers_count = 0;

            for(int i = 0; i < 6; ++i) {
                auto* buf = m_buffers->at(i);
                auto lines = buf->takeAll();

                if (lines.isEmpty()) {
                    empty_buffers_count ++;
                    qDebug() << empty_buffers_count;
                    continue;
                }
               // qDebug() << "message flushed";
                *m_streams[i] << lines.join("\n") << "\n";
                m_streams[i]->flush();
            }
            if (empty_buffers_count == 6) {
                break;
            }

        }
    }

    qDebug() << "flush ended";
    closeFiles();
    emit writingFinished();
}

void CsvWriter::onTimerFineshed()
{
    qDebug() << "onTimerFinished";
    finished = true;
}

void CsvWriter::closeFiles()
{
    qDebug() << "file closing";
    for(int i = 0; i < 6; ++i) {
        m_files[i]->close();
    }
}

void CsvBuffer::append(const QByteArray &line)
{
    QMutexLocker lock(&mutex);
    lines.append(line);
}

QVector<QByteArray> CsvBuffer::takeAll()
{
    QMutexLocker lock(&mutex);
    QVector<QByteArray> out = std::move(lines);
    lines.clear();
    return out;
}

