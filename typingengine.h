#ifndef TYPINGENGINE_H
#define TYPINGENGINE_H

#include <QObject>
#include <QChar>
#include <QList>
#include <QString>
#include <QTimer>

enum class ActionKind {
    Char,
    Enter,
    Tab,
    Backspace
};

struct TypeAction {
    ActionKind kind = ActionKind::Char;
    QChar ch;
};

class DelayService
{
public:
    void reset();
    void setWpm(int wpm);
    int wpm() const { return targetWpm; }
    int forAction(const TypeAction &action);
    int forWrongKey();
    int realizePause() const;
    int rapidBackspace() const;

private:
    int scaled(int ms) const;
    int targetWpm = 45;
    int wordBaseMs = 70;
    bool inWord = false;
};

class TypingEngine : public QObject
{
    Q_OBJECT
public:
    explicit TypingEngine(QObject *parent = nullptr);

    void start(const QString &text, bool autoIndent);
    void pause();
    void resume();
    void reset();
    void setWpm(int wpm);
    bool isRunning() const { return running; }
    bool canResume() const;
    int currentIndex() const { return index; }
    int actionCount() const { return actions.size(); }
    int percent() const;

signals:
    void finished();
    void progressChanged(int percent);

private:
    enum class Phase {
        Idle,
        Typing,
        TypoInsert,
        TypoContinue,
        TypoRealize,
        TypoDelete
    };

    void onTick();
    void schedule();
    void abortTypoCleanup();
    void finishRun();
    void notifyProgress();
    void watchForeground();
    void freezeTarget();
    void sendAction(const TypeAction &action);
    void sendWrongKey();
    void sendChar(QChar ch);
    void sendVirtualKey(quint16 vk);
    bool shouldStartTypo() const;
    void beginTypo();
    bool isTypeableChar(const TypeAction &action) const;
    QList<TypeAction> buildActions(const QString &text, bool autoIndent) const;
    TypeAction fromChar(QChar c) const;
    QChar neighborOf(QChar c) const;

    DelayService delays;
    QTimer *timer = nullptr;
    QTimer *watchTimer = nullptr;
    QList<TypeAction> actions;
    Phase phase = Phase::Idle;
    int index = 0;
    int typosLeft = 0;
    int continueLeft = 0;
    int continueOffset = 0;
    int toDelete = 0;
    bool running = false;
    void *lastRoot = nullptr;
    void *lastControl = nullptr;
    void *targetRoot = nullptr;
    void *targetControl = nullptr;
};

#endif // TYPINGENGINE_H
