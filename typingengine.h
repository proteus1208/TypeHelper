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
    int forAction(const TypeAction &action);
    int forWrongKey();
    int realizePause() const;
    int rapidBackspace() const;
    int settlePause() const;

private:
    int wordBaseMs = 70;
    bool inWord = false;
};

class TypingEngine : public QObject
{
    Q_OBJECT
public:
    explicit TypingEngine(QObject *parent = nullptr);

    void start(const QString &text, bool autoIndent);
    void stop();
    bool isRunning() const { return running; }

signals:
    void finished();

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
    void sendAction(const TypeAction &action);
    void sendWrongKey();
    bool shouldStartTypo() const;
    void beginTypo();
    bool isTypeableChar(const TypeAction &action) const;
    QList<TypeAction> buildActions(const QString &text, bool autoIndent) const;
    TypeAction fromChar(QChar c) const;
    QChar neighborOf(QChar c) const;

    DelayService delays;
    QTimer *timer = nullptr;
    QList<TypeAction> actions;
    Phase phase = Phase::Idle;
    int index = 0;
    int typosLeft = 0;
    int continueLeft = 0;
    int continueOffset = 0;
    int toDelete = 0;
    bool running = false;
};

#endif // TYPINGENGINE_H
