// Author: Vyacheslav Verkhovin, arhiv6@gmail.com

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QStatusBar>
#include <QUrl>
#include <QtEndian>
#include <QDebug>
#include <QNetworkDatagram>
#include <QFile>
#include <QTextStream>

//------------------------

//#include <iostream>
//#include <fstream>
//#include <string>

const auto ERR_TIME_MS = 5000;   // сколько показывать ошибку
const auto POLL_TIME_MS = 1000;  // период сбора статистики
const auto k = 0.01;  // коэффициент фильтрации, 0.0-1.0, Чем он меньше, тем плавнее фильтр

const auto BUF_NUMBER = 6;

const uint8_t protocol_version = 2;

struct packet_header                        // Заголовок пакета
{
    uint8_t protocol_version;               // Версия протокола
    uint8_t counter;                        // Счётчик переданных пакетов
    struct
    {
        uint8_t overflow            : 1;    // Флаг переполнения буфера
        uint8_t                     : 7;
    } flasgs;
    uint8_t                         : 8;
    uint32_t timestamp;                     // Метка времени пакета
    uint16_t buf_size;                      // Сколько байт данных доступно в буфере
    uint16_t data_size;                     // Сколько байт данных следует в пакете
} __attribute__((packed));

static_assert(sizeof(struct packet_header) == 12, "bad size packet_header");

//--------------------------------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    reset_flag = false;

    tcp_socket = new QTcpSocket(this);
    tcp_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(tcp_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError)
#else
    connect(tcp_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, [this](QAbstractSocket::SocketError)
#endif
    {
        statusBar()->showMessage("TCP " + tcp_socket->errorString(), ERR_TIME_MS);
    });

    connect(tcp_socket, &QTcpSocket::stateChanged, this, [this](int state)
    {
        if (state == QAbstractSocket::UnconnectedState)
        {
            ui->connectButton->setText(tr("Connect"));
            timer->stop();                                  // Stop polling
        }
        else if (state == QAbstractSocket::ConnectedState)
        {
            ui->connectButton->setText(tr("Disconnect"));
            timer->start(POLL_TIME_MS);                      // Start polling
        }
    });

    connect(tcp_socket, &QTcpSocket::readyRead, this, &MainWindow::readyReadTcp);

    //----------------------------------------------------------------------------------------------

    udp_socket = new QUdpSocket(this);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(udp_socket, &QUdpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError)
#else
    connect(udp_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, [this](QAbstractSocket::SocketError)
#endif
    {
        statusBar()->showMessage("UDP " + udp_socket->errorString(), ERR_TIME_MS);
    });
    connect(udp_socket, &QUdpSocket::stateChanged, this, [this](int state)
            {
                if (state == QAbstractSocket::UnconnectedState)
                {
                    recieve_timer->stop();                                  // Stop polling
                }
                else if (state == QAbstractSocket::ConnectedState)
                {
                    qDebug() << "Connection established";                    // Start polling
                }
            });
    connect(udp_socket, &QUdpSocket::readyRead, this, &MainWindow::readyReadUdp);

    //----------------------------------------------------------------------------------------------

    timer = new QTimer(this);
    recieve_timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::secTimeout);

    //----------------------------------------------------------------------------------------------

    QString appName = QString("MLAFAR_Streamer_demo");
    this->setWindowTitle(appName);

    settings = new QSettings(QSettings::IniFormat, QSettings::UserScope, "NIIRTS", appName);

    this->restoreGeometry(settings->value("windowGeometry").toByteArray());
    this->restoreState(settings->value("windowsState").toByteArray());
    ui->address->setText(settings->value("address", "localhost").toString());

    //----------------------------------------------------------------------------------------------


    blocking_queue = new BlockingQueue;
    processor_thread = new QThread;
    m_processor = new Processor(blocking_queue);
    m_processor->moveToThread(processor_thread);
    connect(recieve_timer, &QTimer::timeout, m_processor, &Processor::onTimerFinished,
        Qt::QueuedConnection);

    connect(recieve_timer, &QTimer::timeout, this, []()
            {
                 qDebug() << "timer ended";
             });

    connect(recieve_timer, &QTimer::timeout, this, [this]()
            {
                udp_socket->disconnectFromHost();
            });

    connect(processor_thread, &QThread::started,
             m_processor, &Processor::processData);
    connect(m_processor, &Processor::processorFinished,
            this, []() {
        qDebug() << "processor Finished";
    });
    connect(m_processor, &Processor::processorFinished,
             processor_thread, &QThread::quit);
    connect(m_processor, &Processor::processorFinished,
            m_processor, &Processor::deleteLater);
    connect(processor_thread, &QThread::finished,
             processor_thread, &QThread::deleteLater);

}

MainWindow::~MainWindow()
{
    settings->setValue("windowGeometry", this->saveGeometry());
    settings->setValue("windowsState", this->saveState());
    settings->setValue("address", ui->address->text());
    settings->sync();

    tcp_socket->close();
    udp_socket->close();
}

void MainWindow::on_connectButton_clicked()
{
    statusBar()->clearMessage();

    qDebug() << "Mainwindow  thread id";
    qDebug() << QThread::currentThreadId();
    if (tcp_socket->state() != QTcpSocket::ConnectedState)
    {
        const QUrl url = QUrl::fromUserInput(ui->address->text());
        tcp_socket->connectToHost(url.host(), url.port(9999));
        udp_socket->bind(QHostAddress::Any, url.port(9999));

        stat_reset();

        recieve_timer->start(5000);
        processor_thread->start();
    }
    else
    {
        tcp_socket->disconnectFromHost();
        udp_socket->disconnectFromHost();
    }
}

void MainWindow::on_reset_clicked()
{
    reset_flag = true;
}

void MainWindow::stat_reset()
{
    stat = {};

    printStat();

    stat.was_packets = false;
    stat.was_print = false;

    stat.min = {.packet_counter     = __INT_MAX__,
                .packet_size        = __INT_MAX__,
                .byte_counter       = __INT_MAX__,
                .loss_counter       = __INT_MAX__,
                .error_counter      = __INT_MAX__,
                .overload_counter   = __INT_MAX__,
                .speed              = __INT_MAX__,
                .fullness           = 100.0,
                .delay              = 1e10,
               };

    stat.t_prev = 0;
}

void MainWindow::printStat()
{
    ui->seconds->setNum(stat.seconds);

#define show_stat_num(name, type) ui->name##_##type->setText(QString("%1").arg(stat.type.name))
#define show_stat_MB(name, type) ui->name##_##type->setText(QString("%1").arg(1.0*stat.type.name/1024.0/1024.0, 0, 'f', 3))
#define show_stat_per(name, type) ui->name##_##type->setText(QString("%1").arg(stat.type.name, 0, 'f', 3))

#define show_stat_all(type, name)  \
    show_stat_##type(name, sec);   \
    show_stat_##type(name, min);   \
    show_stat_##type(name, avg);   \
    show_stat_##type(name, max);   \
    show_stat_##type(name, total);

    show_stat_all(num, packet_counter);
    show_stat_all(MB, byte_counter);

    show_stat_num(packet_size, min);
    show_stat_num(packet_size, avg);
    show_stat_num(packet_size, max);

    show_stat_MB(speed, sec);
    show_stat_MB(speed, min);
    show_stat_MB(speed, avg);
    show_stat_MB(speed, max);

    show_stat_all(num, error_counter);
    show_stat_all(num, loss_counter);
    show_stat_all(num, overload_counter);

    show_stat_per(fullness, min);
    show_stat_per(fullness, avg);
    show_stat_per(fullness, max);

    show_stat_per(delay, min);
    show_stat_per(delay, avg);
    show_stat_per(delay, max);
}

void MainWindow::secTimeout()
{
    if (reset_flag)
    {
        stat_reset();
        reset_flag = false;
        return;
    }

    stat.sec.speed = stat.sec.byte_counter * 8;

#define calc_stat(name)                                                         \
    stat.total.name += stat.sec.name;                                           \
    if (stat.sec.name < stat.min.name) {stat.min.name = stat.sec.name;}         \
    if (stat.sec.name > stat.max.name) {stat.max.name = stat.sec.name;}         \
    if (stat.seconds == 0) {stat.avg.name = stat.sec.name;}                     \
    else {stat.avg.name += (stat.sec.name - stat.avg.name) * k;}

    calc_stat(byte_counter);
    calc_stat(packet_counter);
    calc_stat(speed);

    calc_stat(error_counter);
    calc_stat(loss_counter);
    calc_stat(overload_counter);

    printStat();
    stat.sec = {};
    stat.seconds++;
}

bool MainWindow::checkHeader(struct packet_header *packet_header)
{
    // разбор заголовка пакета

    if (packet_header->protocol_version != protocol_version)
    {
        qDebug() << "version";
        stat.sec.error_counter++; // версия протокола не совпадает
        return false;
    }

    if (packet_header->counter != (++stat.packet_counter))
    {
        if (stat.seconds)
        {
            qDebug() << "counter " << stat.packet_counter << packet_header->counter;
            stat.sec.loss_counter++; // был потерян пакет
        }
        stat.packet_counter = packet_header->counter;
    }

    if (packet_header->flasgs.overflow)
    {
        stat.sec.overload_counter++; // было переполнение буферов
    }

    qint64 packet_size = sizeof(struct packet_header) + packet_header->data_size;
    if (packet_size < stat.min.packet_size) {stat.min.packet_size = packet_size;}
    if (packet_size > stat.max.packet_size) {stat.max.packet_size = packet_size;}
    if (stat.avg.packet_size == 0) {stat.avg.packet_size = packet_size;}
    else {stat.avg.packet_size += (packet_size - stat.avg.packet_size) * k;}

    float fullness = 100.0 * packet_header->buf_size / (BUF_NUMBER * 1100 * 4);
    if (fullness < stat.min.fullness) {stat.min.fullness = fullness;}
    if (fullness > stat.max.fullness) {stat.max.fullness = fullness;}
    if (stat.avg.fullness == 0) {stat.avg.fullness = fullness;}
    else {stat.avg.fullness += (fullness - stat.avg.fullness) * k;}

    float t_now = packet_header->timestamp;
    float delay = t_now - stat.t_prev;
    if (stat.t_prev)
    {
        if (delay < stat.min.delay) {stat.min.delay = delay;}
        if (delay > stat.max.delay) {stat.max.delay = delay;}
        if (stat.avg.delay == 0) {stat.avg.delay = delay;}
        else {stat.avg.delay += (delay - stat.avg.delay) * k;}
    }
    stat.t_prev = t_now;

    return true;
}

void MainWindow::readyReadTcp()
{
    while (tcp_socket->bytesAvailable() >= sizeof(struct packet_header))
    {
        stat.sec.packet_counter++;
        stat.sec.byte_counter += sizeof(packet_header);

        struct packet_header packet_header;
        tcp_socket->read((char *)&packet_header, sizeof(packet_header));

        if (checkHeader(&packet_header) != true)
        {
            tcp_socket->readAll(); // clear buffer
            return;
        }

        // приём данных пакета

        qint64 data_size = packet_header.data_size;
        while (data_size)
        {
            QByteArray data = tcp_socket->read(data_size);
            stat.sec.byte_counter += data.size();
            data_size -= data.size();

            if (data_size)
            {
                if (tcp_socket->waitForReadyRead(100) == false)
                {
                    stat.sec.error_counter++; // не хватило данных
                    return;
                }
            }
        }
    }
}

void MainWindow::readyReadUdp()
{
    while (udp_socket->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = udp_socket->receiveDatagram();

        stat.sec.packet_counter++;
        // stat.sec.byte_counter += datagram.data().size();

        // struct packet_header *packet_header =
        //     reinterpret_cast<struct packet_header *>(datagram.data().data());

        // if (checkHeader(packet_header) != true)
        // {
        //     return;
        // }

        // // проверка данных пакета

        // if ((sizeof(struct packet_header) + packet_header->data_size) != datagram.data().size())
        // {
        //     stat.sec.error_counter++; // не хватило данных
        // }

        // указатель на начало полезных данных

        const char *payload_data = datagram.data().data();// + sizeof(struct packet_header);
     //   QByteArray dat = payload_data;
        //size_t payload_size = packet_header->data_size;
   //     qDebug() << dat.size();
        // Отправка данных в буффер
        blocking_queue->Enqueue(payload_data);
    }
}
