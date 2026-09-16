#ifndef STOPOVERLAY_H
#define STOPOVERLAY_H

#include <QWidget>
#include <QPoint>

class QParallelAnimationGroup;
class QPushButton;

class StopOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit StopOverlay(QWidget *parent = nullptr);

    void appear();
    void disappear();

signals:
    void stopRequested();
    void hiddenFully();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void placeOnScreen(int yOffset);
    QPoint restPos() const;
    void stopAnimations();

    QPushButton *button = nullptr;
    QParallelAnimationGroup *currentAnim = nullptr;
    bool closing = false;
};

#endif // STOPOVERLAY_H
