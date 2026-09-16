#include "stopoverlay.h"

#include <QCursor>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QEasingCurve>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QtGlobal>

StopOverlay::StopOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("TypeHelper"));

    auto *pill = new QFrame(this);
    pill->setObjectName(QStringLiteral("progressPill"));
    pill->setFixedSize(112, 36);
    pill->setStyleSheet(QStringLiteral(
        "#progressPill {"
        "  background-color: #dc2626;"
        "  border: none;"
        "  border-radius: 18px;"
        "}"
        "QLabel#percentLabel {"
        "  background: transparent;"
        "  color: #ffffff;"
        "}"
        "QPushButton#closeBtn {"
        "  background: transparent;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 14px;"
        "}"
        "QPushButton#closeBtn:hover {"
        "  background-color: rgba(0, 0, 0, 90);"
        "}"
        "QPushButton#closeBtn:pressed {"
        "  background-color: rgba(0, 0, 0, 140);"
        "}"));

    percentLabel = new QLabel(QStringLiteral("0%"), pill);
    percentLabel->setObjectName(QStringLiteral("percentLabel"));
    percentLabel->setAlignment(Qt::AlignCenter);
    percentLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont percentFont = percentLabel->font();
    percentFont.setPixelSize(13);
    percentFont.setBold(true);
    percentLabel->setFont(percentFont);

    closeBtn = new QPushButton(QStringLiteral("✕"), pill);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFocusPolicy(Qt::NoFocus);
    closeBtn->setFixedSize(28, 28);
    QFont closeFont = closeBtn->font();
    closeFont.setPixelSize(13);
    closeFont.setBold(true);
    closeBtn->setFont(closeFont);

    auto *leftPad = new QWidget(pill);
    leftPad->setFixedSize(28, 28);
    leftPad->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *row = new QHBoxLayout(pill);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(0);
    row->addWidget(leftPad);
    row->addWidget(percentLabel, 1);
    row->addWidget(closeBtn);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->addWidget(pill);

    auto *shadow = new QGraphicsDropShadowEffect(pill);
    shadow->setBlurRadius(20);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 140));
    pill->setGraphicsEffect(shadow);

    connect(closeBtn, &QPushButton::clicked, this, &StopOverlay::stopRequested);

    adjustSize();
    setFixedSize(sizeHint());
}

void StopOverlay::setProgress(int percent)
{
    percentLabel->setText(QStringLiteral("%1%").arg(qBound(0, percent, 100)));
}

void StopOverlay::appear()
{
    closing = false;
    hideNotified = false;
    stopAnimations();
    placeOnScreen(-22);
    setWindowOpacity(0.0);
    show();
    raise();

    auto *group = new QParallelAnimationGroup(this);
    auto *posAnim = new QPropertyAnimation(this, "pos");
    posAnim->setDuration(280);
    posAnim->setStartValue(pos());
    posAnim->setEndValue(restPos());
    posAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *opAnim = new QPropertyAnimation(this, "windowOpacity");
    opAnim->setDuration(280);
    opAnim->setStartValue(windowOpacity());
    opAnim->setEndValue(1.0);
    opAnim->setEasingCurve(QEasingCurve::OutCubic);

    group->addAnimation(posAnim);
    group->addAnimation(opAnim);
    currentAnim = group;
    connect(group, &QParallelAnimationGroup::finished, this, [this, group]() {
        if (currentAnim == group)
            currentAnim = nullptr;
        group->deleteLater();
    });
    group->start();
}

void StopOverlay::disappear()
{
    if (closing) {
        hideFully();
        return;
    }
    closing = true;
    stopAnimations();
    if (!isVisible()) {
        hideFully();
        return;
    }

    auto *group = new QParallelAnimationGroup(this);
    auto *posAnim = new QPropertyAnimation(this, "pos");
    posAnim->setDuration(180);
    posAnim->setStartValue(pos());
    posAnim->setEndValue(QPoint(pos().x(), pos().y() - 18));
    posAnim->setEasingCurve(QEasingCurve::InCubic);

    auto *opAnim = new QPropertyAnimation(this, "windowOpacity");
    opAnim->setDuration(180);
    opAnim->setStartValue(windowOpacity());
    opAnim->setEndValue(0.0);
    opAnim->setEasingCurve(QEasingCurve::InCubic);

    group->addAnimation(posAnim);
    group->addAnimation(opAnim);
    currentAnim = group;
    connect(group, &QParallelAnimationGroup::finished, this, [this, group]() {
        if (currentAnim == group)
            currentAnim = nullptr;
        group->deleteLater();
        hideFully();
    });
    group->start();
    QTimer::singleShot(220, this, [this]() {
        if (closing && isVisible())
            hideFully();
    });
}

void StopOverlay::hideFully()
{
    stopAnimations();
    hide();
    setWindowOpacity(1.0);
    setProgress(0);
    if (hideNotified)
        return;
    hideNotified = true;
    emit hiddenFully();
}

void StopOverlay::placeOnScreen(int yOffset)
{
    move(restPos() + QPoint(0, yOffset));
}

QPoint StopOverlay::restPos() const
{
    const QPoint cursor = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(cursor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect g = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const int x = g.x() + (g.width() - width()) / 2;
    const int y = g.y() + 50;
    return QPoint(x, y);
}

void StopOverlay::stopAnimations()
{
    if (!currentAnim)
        return;
    currentAnim->stop();
    currentAnim->deleteLater();
    currentAnim = nullptr;
}
