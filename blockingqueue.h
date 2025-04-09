#ifndef BLOCKINGQUEUE_H
#define BLOCKINGQUEUE_H

#include <QByteArray>
#include <QMutex>
#include <QQueue>

class BlockingQueue
{
public:
   BlockingQueue();
   void Enqueue(QByteArray);
   QByteArray Dequeue();
   qsizetype size();

private:
    QQueue<QByteArray> queue;
    QMutex mut_;
};

#endif // BLOCKINGQUEUE_H
