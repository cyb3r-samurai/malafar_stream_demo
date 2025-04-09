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

    connect(udp_socket, &QUdpSocket::readyRead, this, &MainWindow::readyReadUdp);

    //----------------------------------------------------------------------------------------------

    timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, &MainWindow::secTimeout);

    //----------------------------------------------------------------------------------------------

    QString appName = QString("MLAFAR_Streamer_demo");
    this->setWindowTitle(appName);

    settings = new QSettings(QSettings::IniFormat, QSettings::UserScope, "NIIRTS", appName);

    this->restoreGeometry(settings->value("windowGeometry").toByteArray());
    this->restoreState(settings->value("windowsState").toByteArray());
    ui->address->setText(settings->value("address", "localhost").toString());
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
    if (tcp_socket->state() != QTcpSocket::ConnectedState)
    {
        const QUrl url = QUrl::fromUserInput(ui->address->text());
        tcp_socket->connectToHost(url.host(), url.port(9999));
        udp_socket->bind(QHostAddress::Any, url.port(9999));

        stat_reset();
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
    // получаем количество секунд для записи из QLineEdit

    // bool ok;
    //    int recordTimeSeconds = ui->recordTimeLineEdit->text().toInt(&ok);

    //    if (!ok || recordTimeSeconds <= 0)
    //    {
    //        qDebug() << "Invalid time entered in QLineEdit";
    //        return;
    //    }

    //    static qint64 startTime = 0;
    //    qint64 currentTime = QDateTime::currentSecsSinceEpoch();

    //    // если время записи не начато, запустим отсчет времени

    //    if (startTime == 0)
    //    {
    //        startTime = currentTime;
    //    }

    //    // если прошло больше времени, чем указано в QLineEdit, прекращаем запись

    //    if (currentTime - startTime >= recordTimeSeconds)
    //    {
    //        qDebug() << "Recording time reached. Stopping.";
    //        return;  // останавливаем обработку пакетов, если время записи закончилось
    //    }

            while (udp_socket->hasPendingDatagrams())
            {
                QNetworkDatagram datagram = udp_socket->receiveDatagram();

                stat.sec.packet_counter++;
                stat.sec.byte_counter += datagram.data().size();

                struct packet_header *packet_header =
                        reinterpret_cast<struct packet_header *>(datagram.data().data());

                if (checkHeader(packet_header) != true)
                {
                    return;
                }

                // проверка данных пакета

                if ((sizeof(struct packet_header) + packet_header->data_size) != datagram.data().size())
                {
                    stat.sec.error_counter++; // не хватило данных
                }


        // указатель на начало полезных данных

        const char *payload_data = datagram.data().data() + sizeof(struct packet_header);
        size_t payload_size = packet_header->data_size;

        // Обработка полезных данных

        processData(payload_data, payload_size);

        //реализация сохранения в csv файл

         // const char *payload_data = datagram.data().data() + sizeof(struct packet_header);
         //         size_t payload_size = packet_header->data_size;

//                 QByteArray payload(payload_data, payload_size);

//                 // открытие CSV файла для добавления новых строк

//                 QFile file("received_data.csv");
//                        if (file.open(QIODevice::Append | QIODevice::Text))
//                        {
//                            QTextStream out(&file);

//                            // если файл только создан, добавим заголовок

//                            if (file.size() == 0)
//                            {
//                                out << "Payload Size,Payload Data (Hex)\n";
//                            }

//                            // Форматируем данные для строки CSV

//                            QString payloadHex = payload.toHex(' ');


//                            out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << ", ";
//                            out << payload.toHex(' ') << '\n';
// //

//                            file.close();
//                        }

        //Реализация сохранения в txt файл

//        const char *payload_data = datagram.data().data() + sizeof(struct packet_header);
//                size_t payload_size = packet_header->data_size;

//                QByteArray payload(payload_data, payload_size);

//                QFile file("received_data.txt");
//                if (file.open(QIODevice::Append | QIODevice::Text))
//                {
//                    QTextStream out(&file);
////                    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << ": ";
//                    out << payload.toHex(' ') << '\n';
//                    file.close();
//                }
    }
}

void MainWindow::startRecording(int duration_ms)
{
    recording = true;

    // таймер для остановки записи через указанное время

    QTimer::singleShot(duration_ms, this, &MainWindow::stopRecording);
}

void MainWindow::stopRecording()
{
    recording = false;
    qDebug() << "Recording stopped.";
}

void MainWindow::processData(const char *data, size_t size)
{
//    // реализация обработки полученных данных

//    QByteArray payload(data, static_cast<int>(size));
//    qDebug() << "Полученные данные:" << payload.toHex();
////    logPacketData(packet_header(),payload);

}

//Симуляция тестового пакета

//void MainWindow::simulateTestPacket()
//{
//    // 1. создаем тестовый заголовок пакета

//    packet_header testHeader;
//    testHeader.protocol_version = protocol_version;
//    testHeader.counter = stat.packet_counter + 1; // увеличиваем счетчик пакетов
//    testHeader.flasgs.overflow = 0;
//    testHeader.timestamp = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

//    // тестовый payload, например строка ""

//    QByteArray testPayload("TestData");
//    testHeader.data_size = testPayload.size();
//    // примерное значение buf_size, можно задать в зависимости от тестовых сценариев
//    testHeader.buf_size = BUF_NUMBER;

//    // 2. собираем полный пакет

//    QByteArray packet;
//    packet.append(reinterpret_cast<const char*>(&testHeader), sizeof(testHeader));
//    packet.append(testPayload);

//    // 3. имитируем обработку полученного пакета
//    //проверить заголовок

//    if (!checkHeader(&testHeader)) {
//        qDebug() << "Ошибка проверки тестового пакета";
//        return;
//    }

//    // логирование (если реализована функция logPacketData)

//    logPacketData(testHeader, testPayload);

//    // передаем данные в обработчик полезной нагрузки

//    processData(packet.data() + sizeof(testHeader), testPayload.size());

//    qDebug() << "Тестовый пакет успешно обработан";
//}


//void MainWindow::logPacketData(const struct packet_header &header, const QByteArray &data) {
//    QFile file("packet_log.txt");  // Имя файла для логирования
//    if (file.open(QIODevice::Append | QIODevice::Text)) {
//        QTextStream out(&file);

//        // Форматируем данные пакета
//        out << "Timestamp: " << header.timestamp << "\n";
//        out << "Packet Size: " << (sizeof(struct packet_header) + header.data_size) << " bytes\n";
//        out << "Data: " << data.toHex() << "\n";
//        out << "------------------------------------\n";

//        file.close();
//    }
//}
