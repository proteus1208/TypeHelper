#include "stopoverlay.h"

#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QEasingCurve>
#include <QGraphicsDropShadowEffect>

StopOverlay::StopOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("TypeHelper"));

    button = new QPushButton(this);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setObjectName(QStringLiteral("stopAutoBtn"));
    button->setStyleSheet(QStringLiteral(
        "QPushButton#stopAutoBtn {"
        "  background-color: #dc2626;"
        "  color: #ffffff;"
        "  border: 2px solid #ffffff;"
        "  border-radius: 10px;"
        "  padding: 10px 22px;"
        "}"
        "QPushButton#stopAutoBtn:hover {"
        "  background-color: #b91c1c;"
        "}"
        "QPushButton#stopAutoBtn:pressed {"
        "  background-color: #991b1b;"
        "}"
        "QLabel { background: transparent; color: #ffffff; }"));

    auto *icon = new QLabel(QStringLiteral("✕"), button);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont iconFont = icon->font();
    iconFont.setPixelSize(18);
    iconFont.setBold(true);
    icon->setFont(iconFont);
    icon->setFixedWidth(28);

    auto *text = new QLabel(QStringLiteral("Stop auto typing, switching to Hot Key - Esc."), button);
    text->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    text->setAttribute(Qt::WA_TransparentForMouseEvents);
    text->setWordWrap(false);
    QFont textFont = text->font();
    textFont.setPixelSize(13);
    textFont.setWeight(QFont::DemiBold);
    text->setFont(textFont);

    auto *row = new QHBoxLayout(button);
    row->setContentsMargins(18, 6, 18, 6);
    row->setSpacing(10);
    row->addStretch();
    row->addWidget(icon);
    row->addWidget(text);
    row->addStretch();

    button->setMinimumHeight(44);
    button->setMinimumWidth(520);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->addWidget(button);

    auto *shadow = new QGraphicsDropShadowEffect(button);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 140));
    button->setGraphicsEffect(shadow);

    connect(button, &QPushButton::clicked, this, &StopOverlay::stopRequested);

    adjustSize();
    setFixedSize(sizeHint());
}

void StopOverlay::appear()
{
    closing = false;
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
    if (closing && !isVisible()) {
        emit hiddenFully();
        return;
    }
    closing = true;
    stopAnimations();
    if (!isVisible()) {
        emit hiddenFully();
        return;
    }

    auto *group = new QParallelAnimationGroup(this);
    auto *posAnim = new QPropertyAnimation(this, "pos");
    posAnim->setDuration(220);
    posAnim->setStartValue(pos());
    posAnim->setEndValue(QPoint(pos().x(), pos().y() - 22));
    posAnim->setEasingCurve(QEasingCurve::InCubic);

    auto *opAnim = new QPropertyAnimation(this, "windowOpacity");
    opAnim->setDuration(220);
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
        hide();
        emit hiddenFully();
    });
    group->start();
}

void StopOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
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
    const int y = g.y() + 100;
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
