#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QAbstractNativeEventFilter>

class QCheckBox;
class QCloseEvent;
class QEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class StopOverlay;
class TypingEngine;

class MainWindow : public QWidget, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartClicked();
    void onResumeClicked();
    void onTypingFinished();
    void requestStop();
    void onOverlayHidden();
    void pollGuards();
    void onTextChanged();

private:
    void setupUi();
    void applyTheme();
    void beginSession(bool resume);
    void endSession(bool completed);
    void registerEscHotkey(bool enable);
    void updateCounts();
    void updateActionButtons();

    QPlainTextEdit *input = nullptr;
    QPushButton *startBtn = nullptr;
    QPushButton *resumeBtn = nullptr;
    QCheckBox *autoIndentBox = nullptr;
    QLabel *countLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QTimer *guardTimer = nullptr;
    TypingEngine *engine = nullptr;
    StopOverlay *overlay = nullptr;

    bool sessionActive = false;
    bool endingSession = false;
    bool clickArmed = false;
    bool completedRun = false;
    QString sessionText;
    static constexpr int kEscHotkeyId = 1;
};

#endif // MAINWINDOW_H
