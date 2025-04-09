// Author: Vyacheslav Verkhovin, arhiv6@gmail.com

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "blockingqueue.h"

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QSettings>
#include <QProgressBar>
#include <QTcpSocket>
#include <QUdpSocket>

#include <QDateTime>
#include <QTime>

QT_BEGIN_NAMESPACE
class QModbusClient;
class QModbusReply;
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_connectButton_clicked();
    void on_reset_clicked();
    bool checkHeader(struct packet_header *packet_header);

    void stat_reset();
    void secTimeout();
    void readyReadTcp();
    void readyReadUdp();
    void printStat();
    void processData(const  char *data, size_t);
//    void logPacketData(const struct packet_header &header, const QByteArray &data);
//    void simulateTestPacket();
    void startRecording(int);
    void stopRecording();


private:

signals:

private:
    Ui::MainWindow *ui;
    QTcpSocket *tcp_socket;
    QUdpSocket *udp_socket;
    QTimer *timer;
    QSettings *settings;
    BlockingQueue *thread_safe_queue;
    bool reset_flag;

    bool recording;

    struct
    {
        struct
        {
            qint64 packet_counter;
            qint64 packet_size;
            qint64 byte_counter;
            qint64 loss_counter;
            qint64 error_counter;
            qint64 overload_counter;
            qint64 speed;
            float fullness;
            float delay;
        }
        sec, min, avg, max, total;

        float t_prev;
        bool was_packets;
        bool was_print;
        int seconds;
        uint8_t packet_counter;
    }
    stat;
};
#endif // MAINWINDOW_H
