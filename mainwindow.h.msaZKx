#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QApplication>
#include <QWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QTimer>
#include <QKeyEvent>
#include <QDebug>
#include <windows.h>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr) : QWidget(parent) {
        input = new QPlainTextEdit(this);
        startBtn = new QPushButton("Start Typing", this);
        speedSpin = new QSpinBox(this);

        speedSpin->setRange(10, 2000); // milliseconds per char
        speedSpin->setValue(58);

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(input);
        layout->addWidget(speedSpin);
        layout->addWidget(startBtn);
        setLayout(layout);

        connect(startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    }

private slots:
    void onStartClicked() {
        textToType = input->toPlainText();
        currentIndex = 0;

        // Hide or minimize self to give focus to last active window
        this->hide(); // could also use showMinimized();

        if(timer) {
            timer->stop();
            delete timer;
        }

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::typeNextChar);
        timer->start(speedSpin->value());
    }

    void typeNextChar() {
        if (currentIndex < textToType.length()) {

            QChar c = textToType[currentIndex];
            qDebug() << c << c.unicode();

            INPUT ip{};
            ip.type = INPUT_KEYBOARD;
            ip.ki.dwExtraInfo = 0;

            // Qt new line is 0x0A (LF)
            if (c.unicode() == 0x0A) {
                // ---- SEND REAL ENTER ----
                ip.ki.wVk = VK_RETURN;
                ip.ki.dwFlags = 0; // key down
                SendInput(1, &ip, sizeof(INPUT));

                ip.ki.dwFlags = KEYEVENTF_KEYUP; // key up
                SendInput(1, &ip, sizeof(INPUT));
            }
            else {
                // ---- SEND UNICODE CHARACTER ----
                ip.ki.wVk = 0;
                ip.ki.wScan = c.unicode();
                ip.ki.dwFlags = KEYEVENTF_UNICODE;
                SendInput(1, &ip, sizeof(INPUT));

                ip.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                SendInput(1, &ip, sizeof(INPUT));
            }

            currentIndex++;
        }
        else {
            timer->stop();
            delete timer;
            timer = nullptr;

            this->show();
            this->raise();
            this->activateWindow();
        }
    }

private:
    QPlainTextEdit *input;
    QPushButton *startBtn;
    QSpinBox *speedSpin;
    QTimer *timer = nullptr;
    QString textToType;
    int currentIndex = 0;
};

#endif // MAINWINDOW_H
