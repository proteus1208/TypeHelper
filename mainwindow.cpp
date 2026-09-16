#include "mainwindow.h"
#include "stopoverlay.h"
#include "typingengine.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCursor>
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
    resize(640, 560);
    setMinimumSize(520, 420);

    engine = new TypingEngine(this);
    overlay = new StopOverlay(nullptr);
    guardTimer = new QTimer(this);
    guardTimer->setInterval(40);

    setupUi();
    applyTheme();

    QSettings settings(QStringLiteral("TypeHelper"), QStringLiteral("TypeHelper"));
    autoIndentBox->setChecked(settings.value(QStringLiteral("autoIndent"), false).toBool());

    connect(startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(input, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(autoIndentBox, &QCheckBox::toggled, this, [](bool on) {
        QSettings s(QStringLiteral("TypeHelper"), QStringLiteral("TypeHelper"));
        s.setValue(QStringLiteral("autoIndent"), on);
    });
    connect(engine, &TypingEngine::finished, this, &MainWindow::onTypingFinished);
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

    auto *subtitle = new QLabel(
        QStringLiteral("Types into the last focused window like a person — word rhythm, pauses, and rare typos."),
        this);
    subtitle->setObjectName(QStringLiteral("subtitleLabel"));
    subtitle->setWordWrap(true);

    input = new QPlainTextEdit(this);
    input->setObjectName(QStringLiteral("sourceEdit"));
    input->setPlaceholderText(QStringLiteral("Paste the text you want typed…"));
    input->setTabChangesFocus(false);

    auto *editorCard = new QFrame(this);
    editorCard->setObjectName(QStringLiteral("editorCard"));
    auto *editorLayout = new QVBoxLayout(editorCard);
    editorLayout->setContentsMargins(10, 10, 10, 10);
    editorLayout->addWidget(input);

    autoIndentBox = new QCheckBox(QStringLiteral("Keep indent on Enter"), this);
    autoIndentBox->setObjectName(QStringLiteral("indentBox"));

    auto *indentHint = new QLabel(
        QStringLiteral("Turn this on for editors that copy the previous line's indent when you press Enter. "
                       "Matching indent is skipped; extra Tab/space is sent when indent grows, Backspace when it shrinks."),
        this);
    indentHint->setObjectName(QStringLiteral("hintLabel"));
    indentHint->setWordWrap(true);

    countLabel = new QLabel(this);
    countLabel->setObjectName(QStringLiteral("metaLabel"));

    statusLabel = new QLabel(QStringLiteral("Ready"), this);
    statusLabel->setObjectName(QStringLiteral("statusLabel"));

    auto *metaRow = new QHBoxLayout();
    metaRow->addWidget(countLabel);
    metaRow->addStretch();
    metaRow->addWidget(statusLabel);

    startBtn = new QPushButton(QStringLiteral("Start auto typing"), this);
    startBtn->setObjectName(QStringLiteral("startBtn"));
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setMinimumHeight(44);

    auto *stopHint = new QLabel(
        QStringLiteral("Focus your editor first. Stop with the red button, Esc, a mouse click, or moving the mouse more than 100 px."),
        this);
    stopHint->setObjectName(QStringLiteral("hintLabel"));
    stopHint->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(4);
    layout->addWidget(editorCard, 1);
    layout->addLayout(metaRow);
    layout->addSpacing(4);
    layout->addWidget(autoIndentBox);
    layout->addWidget(indentHint);
    layout->addSpacing(8);
    layout->addWidget(startBtn);
    layout->addWidget(stopHint);
}

void MainWindow::applyTheme()
{
    setObjectName(QStringLiteral("root"));
    setStyleSheet(QStringLiteral(
        "#root { background-color: #1e1e2e; }"
        "#titleLabel { color: #cdd6f4; font-size: 22px; font-weight: 700; }"
        "#subtitleLabel { color: #a6adc8; font-size: 12px; }"
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
        "#hintLabel { color: #7f849c; font-size: 11px; }"
        "#metaLabel, #statusLabel { color: #9399b2; font-size: 11px; }"
        "#statusLabel { font-weight: 600; }"
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
        "#startBtn:disabled { background-color: #45475a; color: #6c7086; }"));
}

void MainWindow::onStartClicked()
{
    if (sessionActive)
        return;
    if (input->toPlainText().isEmpty())
        return;
    beginSession();
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
    statusLabel->setText(completedRun ? QStringLiteral("Done") : QStringLiteral("Stopped"));
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

    POINT pt{};
    if (GetCursorPos(&pt)) {
        const int dx = pt.x - originPos.x();
        const int dy = pt.y - originPos.y();
        if (dx * dx + dy * dy > 100 * 100) {
            requestStop();
            return;
        }
    }

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
    updateCounts();
    startBtn->setEnabled(!input->toPlainText().isEmpty() && !sessionActive);
}

void MainWindow::beginSession()
{
    endingSession = false;
    completedRun = false;
    clickArmed = false;
    startBtn->setEnabled(false);
    statusLabel->setText(QStringLiteral("Typing…"));

    POINT pt{};
    if (GetCursorPos(&pt))
        originPos = QPoint(pt.x, pt.y);
    else
        originPos = QCursor::pos();

    showMinimized();

    sessionActive = true;
    overlay->appear();
    registerEscHotkey(true);
    guardTimer->start();
    engine->start(input->toPlainText(), autoIndentBox->isChecked());
}

void MainWindow::endSession(bool completed)
{
    if (!sessionActive || endingSession)
        return;
    endingSession = true;
    sessionActive = false;
    completedRun = completed;

    engine->stop();
    guardTimer->stop();
    registerEscHotkey(false);
    startBtn->setEnabled(!input->toPlainText().isEmpty());
    overlay->disappear();
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
    countLabel->setText(QStringLiteral("%1 characters  ·  %2 lines").arg(text.size()).arg(lines));
}
