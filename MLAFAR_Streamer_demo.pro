QT += core gui widgets network

CONFIG += c++11

SOURCES +=  main.cpp \
            blockingqueue.cpp \
            csvwriter.cpp \
            mainwindow.cpp \
            processor.cpp

HEADERS +=  mainwindow.h \
    blockingqueue.h \
    csvwriter.h \
    processor.h

FORMS   +=  mainwindow.ui
