#include "mainwindow.h"
#include "stopoverlay.h"
#include "typingengine.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("TypeHelper"));
    resize(560, 480);
    setMinimumSize(420, 360);

    engine = new TypingEngine(this);
    overlay = new StopOverlay(nullptr);
    guardTimer = new QTimer(this);
    guardTimer->setInterval(40);

    setupUi();
    applyTheme();

    QSettings settings(QStringLiteral("TypeHelper"), QStringLiteral("TypeHelper"));
    autoIndentBox->setChecked(settings.value(QStringLiteral("autoIndent"), false).toBool());

    connect(startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(resumeBtn, &QPushButton::clicked, this, &MainWindow::onResumeClicked);
    connect(input, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(autoIndentBox, &QCheckBox::toggled, this, [this](bool on) {
        QSettings s(QStringLiteral("TypeHelper"), QStringLiteral("TypeHelper"));
        s.setValue(QStringLiteral("autoIndent"), on);
        if (!sessionActive)
            engine->reset();
        updateActionButtons();
    });
    connect(engine, &TypingEngine::finished, this, &MainWindow::onTypingFinished);
    connect(engine, &TypingEngine::progressChanged, overlay, &StopOverlay::setProgress);
    connect(overlay, &StopOverlay::stopRequested, this, &MainWindow::requestStop);
    connect(overlay, &StopOverlay::hiddenFully, this, &MainWindow::onOverlayHidden);
    connect(guardTimer, &QTimer::timeout, this, &MainWindow::pollGuards);

    qApp->installNativeEventFilter(this);
    onTextChanged();
}

MainWindow::~MainWindow()
{
    qApp->removeNativeEventFilter(this);
    registerEscHotkey(false);
    if (overlay) {
        overlay->hide();
        overlay->deleteLater();
        overlay = nullptr;
    }
}

bool MainWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (!sessionActive)
        return false;
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const MSG *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kEscHotkeyId) {
        requestStop();
        return true;
    }
    return false;
}

void MainWindow::setupUi()
{
    auto *title = new QLabel(QStringLiteral("TypeHelper"), this);
    title->setObjectName(QStringLiteral("titleLabel"));

    input = new QPlainTextEdit(this);
    input->setObjectName(QStringLiteral("sourceEdit"));
    input->setPlaceholderText(QStringLiteral("Text to type"));
    input->setTabChangesFocus(false);

    auto *editorCard = new QFrame(this);
    editorCard->setObjectName(QStringLiteral("editorCard"));
    auto *editorLayout = new QVBoxLayout(editorCard);
    editorLayout->setContentsMargins(10, 10, 10, 10);
    editorLayout->addWidget(input);

    autoIndentBox = new QCheckBox(QStringLiteral("Keep indent"), this);
    autoIndentBox->setObjectName(QStringLiteral("indentBox"));

    countLabel = new QLabel(this);
    countLabel->setObjectName(QStringLiteral("metaLabel"));

    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("statusLabel"));

    auto *metaRow = new QHBoxLayout();
    metaRow->addWidget(autoIndentBox);
    metaRow->addStretch();
    metaRow->addWidget(countLabel);
    metaRow->addWidget(statusLabel);

    startBtn = new QPushButton(QStringLiteral("Start"), this);
    startBtn->setObjectName(QStringLiteral("startBtn"));
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setMinimumHeight(40);

    resumeBtn = new QPushButton(QStringLiteral("Resume"), this);
    resumeBtn->setObjectName(QStringLiteral("resumeBtn"));
    resumeBtn->setCursor(Qt::PointingHandCursor);
    resumeBtn->setMinimumHeight(40);
    resumeBtn->setEnabled(false);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->addWidget(startBtn, 1);
    btnRow->addWidget(resumeBtn, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(editorCard, 1);
    layout->addLayout(metaRow);
    layout->addLayout(btnRow);
}

void MainWindow::applyTheme()
{
    setObjectName(QStringLiteral("root"));
    setStyleSheet(QStringLiteral(
        "#root { background-color: #1e1e2e; }"
        "#titleLabel { color: #cdd6f4; font-size: 20px; font-weight: 700; }"
        "#editorCard {"
        "  background-color: #181825;"
        "  border: 1px solid #313244;"
        "  border-radius: 10px;"
        "}"
        "#sourceEdit {"
        "  background-color: #181825;"
        "  color: #cdd6f4;"
        "  border: none;"
        "  font-family: 'Cascadia Mono', 'Consolas', 'Courier New', monospace;"
        "  font-size: 13px;"
        "  selection-background-color: #45475a;"
        "  selection-color: #cdd6f4;"
        "}"
        "#indentBox { color: #cdd6f4; font-size: 13px; spacing: 8px; }"
        "#indentBox::indicator { width: 16px; height: 16px; }"
        "#indentBox::indicator:unchecked {"
        "  border: 1px solid #6c7086;"
        "  border-radius: 4px;"
        "  background: #313244;"
        "}"
        "#indentBox::indicator:checked {"
        "  border: 1px solid #89b4fa;"
        "  border-radius: 4px;"
        "  background: #89b4fa;"
        "}"
        "#metaLabel, #statusLabel { color: #9399b2; font-size: 11px; }"
        "#statusLabel { font-weight: 600; padding-left: 10px; }"
        "#startBtn {"
        "  background-color: #89b4fa;"
        "  color: #1e1e2e;"
        "  border: none;"
        "  border-radius: 10px;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "  padding: 10px 16px;"
        "}"
        "#startBtn:hover { background-color: #b4befe; }"
        "#startBtn:pressed { background-color: #74c7ec; }"
        "#startBtn:disabled { background-color: #45475a; color: #6c7086; }"
        "#resumeBtn {"
        "  background-color: #313244;"
        "  color: #cdd6f4;"
        "  border: none;"
        "  border-radius: 10px;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "  padding: 10px 16px;"
        "}"
        "#resumeBtn:hover { background-color: #45475a; }"
        "#resumeBtn:pressed { background-color: #585b70; }"
        "#resumeBtn:disabled { background-color: #181825; color: #585b70; }"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 10px;"
        "  margin: 6px 3px 6px 0;"
        "  border: none;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #45475a;"
        "  border: none;"
        "  border-radius: 5px;"
        "  min-height: 32px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #6c7086; }"
        "QScrollBar::handle:vertical:pressed { background: #89b4fa; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "  width: 0;"
        "  border: none;"
        "  background: none;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QScrollBar:horizontal {"
        "  background: transparent;"
        "  height: 10px;"
        "  margin: 0 6px 3px 6px;"
        "  border: none;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: #45475a;"
        "  border: none;"
        "  border-radius: 5px;"
        "  min-width: 32px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: #6c7086; }"
        "QScrollBar::handle:horizontal:pressed { background: #89b4fa; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "  height: 0;"
        "  width: 0;"
        "  border: none;"
        "  background: none;"
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"));
}

void MainWindow::onStartClicked()
{
    if (sessionActive)
        return;
    if (input->toPlainText().isEmpty())
        return;
    beginSession(false);
}

void MainWindow::onResumeClicked()
{
    if (sessionActive || !engine->canResume())
        return;
    beginSession(true);
}

void MainWindow::onTypingFinished()
{
    endSession(true);
}

void MainWindow::requestStop()
{
    endSession(false);
}

void MainWindow::onOverlayHidden()
{
    if (sessionActive)
        return;
    showNormal();
    raise();
    activateWindow();
    if (completedRun)
        statusLabel->setText(QStringLiteral("Done"));
    else if (engine->canResume())
        statusLabel->setText(QStringLiteral("Paused"));
    else
        statusLabel->clear();
    updateActionButtons();
}

void MainWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && sessionActive && !isMinimized())
        requestStop();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (sessionActive)
        endSession(false);
    registerEscHotkey(false);
    event->accept();
}

void MainWindow::pollGuards()
{
    if (!sessionActive)
        return;

    const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool middle = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    if (!clickArmed) {
        if (!left && !right && !middle)
            clickArmed = true;
        return;
    }
    if (left || right || middle)
        requestStop();
}

void MainWindow::onTextChanged()
{
    if (!sessionActive && engine->canResume() && input->toPlainText() != sessionText)
        engine->reset();
    updateCounts();
    updateActionButtons();
}

void MainWindow::beginSession(bool resume)
{
    endingSession = false;
    completedRun = false;
    clickArmed = false;
    sessionText = input->toPlainText();
    statusLabel->setText(QStringLiteral("Typing"));

    showMinimized();

    sessionActive = true;
    updateActionButtons();
    overlay->appear();
    registerEscHotkey(true);
    guardTimer->start();
    if (resume)
        engine->resume();
    else
        engine->start(sessionText, autoIndentBox->isChecked());
}

void MainWindow::endSession(bool completed)
{
    if (!sessionActive || endingSession)
        return;
    endingSession = true;
    sessionActive = false;
    completedRun = completed;

    if (completed)
        engine->reset();
    else
        engine->pause();

    guardTimer->stop();
    registerEscHotkey(false);
    overlay->disappear();
    updateActionButtons();
}

void MainWindow::registerEscHotkey(bool enable)
{
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (enable)
        RegisterHotKey(hwnd, kEscHotkeyId, 0, VK_ESCAPE);
    else
        UnregisterHotKey(hwnd, kEscHotkeyId);
}

void MainWindow::updateCounts()
{
    const QString text = input->toPlainText();
    int lines = text.isEmpty() ? 0 : text.count(QLatin1Char('\n')) + 1;
    if (text.isEmpty())
        lines = 0;
    countLabel->setText(QStringLiteral("%1 · %2").arg(text.size()).arg(lines));
}

void MainWindow::updateActionButtons()
{
    const bool hasText = !input->toPlainText().isEmpty();
    startBtn->setEnabled(hasText && !sessionActive);
    resumeBtn->setEnabled(!sessionActive && engine->canResume());
}
