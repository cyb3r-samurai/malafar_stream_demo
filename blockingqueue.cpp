#include "blockingqueue.h"
#include <QDebug>

BlockingQueue::BlockingQueue() {}

void BlockingQueue::Enqueue(QByteArray msg)
{
    QMutexLocker lock(&mut_);
    queue.enqueue(msg);
  //  qDebug() << "message queued";
}

QByteArray BlockingQueue::Dequeue()
{
    QMutexLocker lock(&mut_);
    if (queue.isEmpty()) {
        return {};
    }
    return queue.dequeue();
}

qsizetype BlockingQueue::size()
{
    QMutexLocker lock(&mut_);
    return queue.size();
}
