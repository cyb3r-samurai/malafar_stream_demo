QT += core gui widgets network

CONFIG += c++11

SOURCES +=  main.cpp \
            blockingqueue.cpp \
            mainwindow.cpp

HEADERS +=  mainwindow.h \
    blockingqueue.h

FORMS   +=  mainwindow.ui
