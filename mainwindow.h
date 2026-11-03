#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class StopOverlay;
class TypingEngine;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartClicked();
    void onResumeClicked();
    void onTypingFinished();
    void requestStop();
    void onOverlayHidden();
    void onTextChanged();
    void onSpeedChanged(int wpm);

private:
    void setupUi();
    void applyTheme();
    void beginSession(bool resume);
    void endSession(bool completed);
    void updateCounts();
    void updateActionButtons();
    void updateSpeedLabel(int wpm);

    QPlainTextEdit *input = nullptr;
    QPushButton *startBtn = nullptr;
    QPushButton *resumeBtn = nullptr;
    QCheckBox *autoIndentBox = nullptr;
    QSlider *speedSlider = nullptr;
    QLabel *speedValueLabel = nullptr;
    QLabel *countLabel = nullptr;
    QLabel *statusLabel = nullptr;
    TypingEngine *engine = nullptr;
    StopOverlay *overlay = nullptr;

    bool sessionActive = false;
    bool endingSession = false;
    bool completedRun = false;
    QString sessionText;
};

#endif // MAINWINDOW_H
