#include "csvwriter.h"

CsvWriter::CsvWriter(QVector<CsvBuffer *> *buffers, QObject * parent)
    : QObject(parent), m_buffers{buffers}
{
    for (int i = 0; i < 6; ++i) {
        QString file_name = QString("report_%1.csv").arg(i+1);
        QFile* f = new QFile(file_name);

        if(!f->open(QIODevice::WriteOnly |
                    QIODevice::Append    |
                    QIODevice::Text       )) {
            qDebug() << " File not open";
        }
        m_files[i] = f;
        m_streams[i] = new QTextStream(f);
    }
}

void CsvWriter::flushBuffers()
{
    for(int i = 0; i < 6; ++i) {
        auto* buf = m_buffers->at(i);
        auto lines = buf->takeAll();

        if (lines.isEmpty()) {
            continue;
        }

        *m_streams[i] << lines.join("\n") << "\n";
    }
}

void CsvBuffer::append(const QString &line)
{
    QMutexLocker lock(&mutex);
    lines.append(line);
}

QVector<QString> CsvBuffer::takeAll()
{
    QMutexLocker lock(&mutex);
    QVector<QString> out = std::move(lines);
    lines.clear();
    return out;
}
