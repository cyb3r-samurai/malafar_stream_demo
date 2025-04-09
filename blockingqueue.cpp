#include "blockingqueue.h"

BlockingQueue::BlockingQueue() {}

void BlockingQueue::Enqueue(QByteArray msg)
{
    QMutexLocker lock(&mut_);
    queue.enqueue(msg);
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
