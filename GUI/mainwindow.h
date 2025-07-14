// Wearable Sensing LSL GUI
// Copyright (C) 2014-2020 Syntrogi Inc dba Intheon.

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QtGui>
#include <QProgressBar>
#include <QPushButton>


namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT


public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void writeToConsole();
    QStringList parseArguments();
    void timerEvent(QTimerEvent *event);

    // For checking impedance
    void onZButtonClicked();

private:
    Ui::MainWindow *ui;
    QProcess *streamer;
    int timerId;
    int counter;
    QProgressBar *progressBar;

    // For checking impedance
    QPushButton *ZButton; // Pointer to your ZButton (if you create it programmatically)
};

#endif // MAINWINDOW_H
