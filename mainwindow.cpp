#include "mainwindow.h"
#include "stopoverlay.h"
#include "typingengine.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QFrame>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("TypeHelper"));
    resize(560, 520);
    setMinimumSize(420, 400);

    engine = new TypingEngine(this);
    overlay = new StopOverlay(nullptr);

    setupUi();
    applyTheme();

    QSettings settings(QStringLiteral("TypeHelper"), QStringLiteral("TypeHelper"));
    autoIndentBox->setChecked(settings.value(QStringLiteral("autoIndent"), false).toBool());
    const int wpm = qBound(15, settings.value(QStringLiteral("wpm"), 45).toInt(), 120);
    speedSlider->setValue(wpm);
    engine->setWpm(wpm);
    updateSpeedLabel(wpm);

    connect(startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(resumeBtn, &QPushButton::clicked, this, &MainWindow::onResumeClicked);
    connect(input, &QPlainTextEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(speedSlider, &QSlider::valueChanged, this, &MainWindow::onSpeedChanged);
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

    onTextChanged();
}

MainWindow::~MainWindow()
{
    if (overlay) {
        overlay->hide();
        overlay->deleteLater();
        overlay = nullptr;
    }
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

    auto *speedName = new QLabel(QStringLiteral("Speed"), this);
    speedName->setObjectName(QStringLiteral("metaLabel"));

    speedSlider = new QSlider(Qt::Horizontal, this);
    speedSlider->setObjectName(QStringLiteral("speedSlider"));
    speedSlider->setRange(15, 120);
    speedSlider->setValue(45);
    speedSlider->setTickPosition(QSlider::TicksBelow);
    speedSlider->setTickInterval(15);
    speedSlider->setPageStep(5);
    speedSlider->setSingleStep(1);

    speedValueLabel = new QLabel(this);
    speedValueLabel->setObjectName(QStringLiteral("speedValueLabel"));
    speedValueLabel->setMinimumWidth(64);
    speedValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *speedRow = new QHBoxLayout();
    speedRow->setSpacing(10);
    speedRow->addWidget(speedName);
    speedRow->addWidget(speedSlider, 1);
    speedRow->addWidget(speedValueLabel);

    auto *speedHint = new QLabel(QStringLiteral("Typical human typing speed is about 45 WPM"), this);
    speedHint->setObjectName(QStringLiteral("hintLabel"));

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
    layout->addLayout(speedRow);
    layout->addWidget(speedHint);
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
        "#metaLabel, #statusLabel, #hintLabel { color: #9399b2; font-size: 11px; }"
        "#speedValueLabel { color: #cdd6f4; font-size: 12px; font-weight: 700; }"
        "#statusLabel { font-weight: 600; padding-left: 10px; }"
        "#speedSlider { min-height: 22px; }"
        "#speedSlider::groove:horizontal {"
        "  height: 6px;"
        "  background: #313244;"
        "  border-radius: 3px;"
        "}"
        "#speedSlider::sub-page:horizontal {"
        "  height: 6px;"
        "  background: #89b4fa;"
        "  border-radius: 3px;"
        "}"
        "#speedSlider::handle:horizontal {"
        "  width: 16px;"
        "  height: 16px;"
        "  margin: -6px 0;"
        "  background: #cdd6f4;"
        "  border-radius: 8px;"
        "}"
        "#speedSlider::handle:horizontal:hover { background: #b4befe; }"
        "#speedSlider::tick-mark:horizontal { color: #6c7086; }"
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (sessionActive)
        endSession(false);
    event->accept();
}

void MainWindow::onTextChanged()
{
    if (!sessionActive && engine->canResume() && input->toPlainText() != sessionText)
        engine->reset();
    updateCounts();
    updateActionButtons();
}

void MainWindow::onSpeedChanged(int wpm)
{
    engine->setWpm(wpm);
    updateSpeedLabel(wpm);
    QSettings settings(QStringLiteral("TypeHelper"), QStringLiteral("TypeHelper"));
    settings.setValue(QStringLiteral("wpm"), wpm);
}

void MainWindow::updateSpeedLabel(int wpm)
{
    if (wpm == 45)
        speedValueLabel->setText(QStringLiteral("%1 WPM  avg").arg(wpm));
    else
        speedValueLabel->setText(QStringLiteral("%1 WPM").arg(wpm));
}

void MainWindow::beginSession(bool resume)
{
    endingSession = false;
    completedRun = false;
    sessionText = input->toPlainText();
    statusLabel->setText(QStringLiteral("Typing"));

    showMinimized();

    sessionActive = true;
    updateActionButtons();
    overlay->appear();
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

    overlay->disappear();
    updateActionButtons();
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
