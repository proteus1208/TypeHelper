#ifndef STOPOVERLAY_H
#define STOPOVERLAY_H

#include <QWidget>
#include <QPoint>

class QLabel;
class QParallelAnimationGroup;
class QPushButton;

class StopOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit StopOverlay(QWidget *parent = nullptr);

    void appear();
    void disappear();
    void setProgress(int percent);

signals:
    void stopRequested();
    void hiddenFully();

private:
    void placeOnScreen(int yOffset);
    QPoint restPos() const;
    void stopAnimations();
    void hideFully();

    QLabel *percentLabel = nullptr;
    QPushButton *closeBtn = nullptr;
    QParallelAnimationGroup *currentAnim = nullptr;
    bool closing = false;
    bool hideNotified = false;
};

#endif // STOPOVERLAY_H
