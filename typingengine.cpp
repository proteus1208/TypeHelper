#include "typingengine.h"

#include <QHash>
#include <QRandomGenerator>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

int uni(int lo, int hiInclusive)
{
    if (hiInclusive <= lo)
        return lo;
    return QRandomGenerator::global()->bounded(lo, hiInclusive + 1);
}

bool chance(int perThousand)
{
    return QRandomGenerator::global()->bounded(1000) < perThousand;
}

QString leadingWhitespace(const QString &line)
{
    int i = 0;
    while (i < line.size() && (line[i] == QLatin1Char(' ') || line[i] == QLatin1Char('\t')))
        ++i;
    return line.left(i);
}

void sendUnicode(ushort code)
{
    INPUT ip[2]{};
    ip[0].type = INPUT_KEYBOARD;
    ip[0].ki.wScan = code;
    ip[0].ki.dwFlags = KEYEVENTF_UNICODE;
    ip[1] = ip[0];
    ip[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, ip, sizeof(INPUT));
}

void sendVk(WORD vk)
{
    INPUT ip[2]{};
    ip[0].type = INPUT_KEYBOARD;
    ip[0].ki.wVk = vk;
    ip[1] = ip[0];
    ip[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, ip, sizeof(INPUT));
}

enum class CharKind {
    Lower,
    Upper,
    Digit,
    Period,
    Special,
    Space,
    Tab,
    Enter,
    Backspace
};

CharKind kindOf(const TypeAction &action)
{
    if (action.kind == ActionKind::Enter)
        return CharKind::Enter;
    if (action.kind == ActionKind::Tab)
        return CharKind::Tab;
    if (action.kind == ActionKind::Backspace)
        return CharKind::Backspace;

    const QChar c = action.ch;
    if (c == QLatin1Char(' ') || c == QChar(0x00A0))
        return CharKind::Space;
    if (c == QLatin1Char('.'))
        return CharKind::Period;
    if (c.isLower())
        return CharKind::Lower;
    if (c.isUpper())
        return CharKind::Upper;
    if (c.isDigit())
        return CharKind::Digit;
    return CharKind::Special;
}

int clampMs(int value, int lo, int hi)
{
    return qBound(lo, value, hi);
}

} // namespace

void DelayService::reset()
{
    wordBaseMs = uni(55, 88);
    inWord = false;
}

int DelayService::forAction(const TypeAction &action)
{
    const CharKind kind = kindOf(action);

    if (kind != CharKind::Lower && kind != CharKind::Upper)
        inWord = false;

    // Rare long pause — thinking about the next keystroke.
    if (kind != CharKind::Backspace && chance(3))
        return uni(5000, 10000);

    switch (kind) {
    case CharKind::Lower: {
        if (!inWord) {
            wordBaseMs = uni(52, 90);
            inWord = true;
        }
        return clampMs(wordBaseMs + uni(-14, 18), 38, 120);
    }
    case CharKind::Upper:
        inWord = true;
        return uni(95, 180);
    case CharKind::Digit:
        return uni(80, 165);
    case CharKind::Special:
        return uni(160, 380);
    case CharKind::Period:
        if (chance(80))
            return uni(5000, 10000);
        return uni(480, 1400);
    case CharKind::Space:
        return uni(90, 220);
    case CharKind::Tab:
        return uni(250, 600);
    case CharKind::Enter:
        if (chance(45))
            return uni(5000, 10000);
        return uni(380, 1100);
    case CharKind::Backspace:
        return uni(70, 140);
    }
    return uni(80, 140);
}

int DelayService::forWrongKey()
{
    return uni(55, 130);
}

int DelayService::realizePause() const
{
    return uni(280, 720);
}

int DelayService::rapidBackspace() const
{
    return uni(42, 88);
}

TypingEngine::TypingEngine(QObject *parent)
    : QObject(parent)
    , timer(new QTimer(this))
{
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &TypingEngine::onTick);
}

void TypingEngine::start(const QString &text, bool autoIndent)
{
    reset();
    actions = buildActions(text, autoIndent);
    if (actions.isEmpty()) {
        emit finished();
        return;
    }

    delays.reset();
    index = 0;
    phase = Phase::Typing;
    running = true;
    notifyProgress();
    timer->start(qBound(520, delays.forAction(actions.first()), 1400));
}

void TypingEngine::pause()
{
    running = false;
    timer->stop();
    abortTypoCleanup();
    phase = Phase::Idle;
}

void TypingEngine::resume()
{
    if (running || !canResume())
        return;
    phase = Phase::Typing;
    running = true;
    notifyProgress();
    timer->start(qBound(520, delays.forAction(actions[index]), 1400));
}

void TypingEngine::reset()
{
    running = false;
    phase = Phase::Idle;
    timer->stop();
    actions.clear();
    index = 0;
    typosLeft = 0;
    continueLeft = 0;
    continueOffset = 0;
    toDelete = 0;
}

bool TypingEngine::canResume() const
{
    return !running && index >= 0 && index < actions.size();
}

int TypingEngine::percent() const
{
    if (actions.isEmpty())
        return 0;
    return qBound(0, (index * 100) / actions.size(), 100);
}

void TypingEngine::notifyProgress()
{
    emit progressChanged(percent());
}

void TypingEngine::abortTypoCleanup()
{
    if (toDelete > 0
        && (phase == Phase::TypoInsert
            || phase == Phase::TypoContinue
            || phase == Phase::TypoRealize
            || phase == Phase::TypoDelete)) {
        for (int i = 0; i < toDelete; ++i)
            sendVk(VK_BACK);
    }
    typosLeft = 0;
    continueLeft = 0;
    continueOffset = 0;
    toDelete = 0;
}

void TypingEngine::finishRun()
{
    running = false;
    phase = Phase::Idle;
    if (!actions.isEmpty())
        emit progressChanged(100);
    actions.clear();
    index = 0;
    emit finished();
}

void TypingEngine::onTick()
{
    if (!running)
        return;

    switch (phase) {
    case Phase::Idle:
        return;

    case Phase::Typing: {
        if (index >= actions.size()) {
            finishRun();
            return;
        }
        if (shouldStartTypo()) {
            beginTypo();
            sendWrongKey();
            --typosLeft;
            ++toDelete;
            if (typosLeft <= 0)
                phase = (continueLeft > 0) ? Phase::TypoContinue : Phase::TypoRealize;
            schedule();
            return;
        }
        sendAction(actions[index]);
        ++index;
        notifyProgress();
        if (index >= actions.size()) {
            finishRun();
            return;
        }
        schedule();
        return;
    }

    case Phase::TypoInsert:
        sendWrongKey();
        --typosLeft;
        ++toDelete;
        if (typosLeft <= 0)
            phase = (continueLeft > 0) ? Phase::TypoContinue : Phase::TypoRealize;
        schedule();
        return;

    case Phase::TypoContinue: {
        const int ahead = index + continueOffset;
        if (continueLeft <= 0 || ahead >= actions.size() || !isTypeableChar(actions[ahead])) {
            phase = Phase::TypoRealize;
            schedule();
            return;
        }
        sendAction(actions[ahead]);
        ++continueOffset;
        --continueLeft;
        ++toDelete;
        if (continueLeft <= 0)
            phase = Phase::TypoRealize;
        schedule();
        return;
    }

    case Phase::TypoRealize:
        phase = Phase::TypoDelete;
        schedule();
        return;

    case Phase::TypoDelete:
        sendVk(VK_BACK);
        --toDelete;
        if (toDelete <= 0) {
            continueOffset = 0;
            phase = Phase::Typing;
        }
        schedule();
        return;
    }
}

void TypingEngine::schedule()
{
    if (!running)
        return;

    int ms = 80;
    switch (phase) {
    case Phase::Idle:
        return;
    case Phase::Typing:
        if (index >= actions.size()) {
            finishRun();
            return;
        }
        ms = delays.forAction(actions[index]);
        break;
    case Phase::TypoInsert:
        ms = delays.forWrongKey();
        break;
    case Phase::TypoContinue: {
        const int ahead = index + continueOffset;
        if (ahead < actions.size())
            ms = delays.forAction(actions[ahead]);
        else
            ms = delays.realizePause();
        break;
    }
    case Phase::TypoRealize:
        ms = delays.realizePause();
        break;
    case Phase::TypoDelete:
        ms = delays.rapidBackspace();
        break;
    }
    timer->start(ms);
}

void TypingEngine::sendAction(const TypeAction &action)
{
    switch (action.kind) {
    case ActionKind::Enter:
        sendVk(VK_RETURN);
        break;
    case ActionKind::Tab:
        sendVk(VK_TAB);
        break;
    case ActionKind::Backspace:
        sendVk(VK_BACK);
        break;
    case ActionKind::Char:
        if (action.ch == QLatin1Char('\t'))
            sendVk(VK_TAB);
        else
            sendUnicode(action.ch.unicode());
        break;
    }
}

void TypingEngine::sendWrongKey()
{
    QChar seed = QLatin1Char('a');
    if (index < actions.size() && actions[index].kind == ActionKind::Char)
        seed = actions[index].ch;
    sendUnicode(neighborOf(seed).unicode());
}

bool TypingEngine::shouldStartTypo() const
{
    if (index >= actions.size() || !isTypeableChar(actions[index]))
        return false;
    // About 2.2% of letters/digits/symbols slip.
    return chance(15);
}

void TypingEngine::beginTypo()
{
    typosLeft = uni(1, 3);
    continueOffset = 0;
    toDelete = 0;

    int maxContinue = 0;
    for (int i = index; i < actions.size() && maxContinue < 4; ++i) {
        if (!isTypeableChar(actions[i]))
            break;
        ++maxContinue;
    }
    continueLeft = (maxContinue == 0) ? 0 : uni(0, maxContinue);
    phase = Phase::TypoInsert;
}

bool TypingEngine::isTypeableChar(const TypeAction &action) const
{
    if (action.kind != ActionKind::Char)
        return false;
    const CharKind kind = kindOf(action);
    return kind == CharKind::Lower
        || kind == CharKind::Upper
        || kind == CharKind::Digit
        || kind == CharKind::Special
        || kind == CharKind::Period;
}

TypeAction TypingEngine::fromChar(QChar c) const
{
    TypeAction action;
    if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
        action.kind = ActionKind::Enter;
        return action;
    }
    if (c == QLatin1Char('\t')) {
        action.kind = ActionKind::Tab;
        action.ch = c;
        return action;
    }
    action.kind = ActionKind::Char;
    action.ch = c;
    return action;
}

QList<TypeAction> TypingEngine::buildActions(const QString &text, bool autoIndent) const
{
    QList<TypeAction> out;
    if (!autoIndent) {
        for (int i = 0; i < text.size(); ++i) {
            const QChar c = text[i];
            if (c == QLatin1Char('\r')) {
                if (i + 1 < text.size() && text[i + 1] == QLatin1Char('\n'))
                    continue;
                out.append(fromChar(QLatin1Char('\n')));
                continue;
            }
            out.append(fromChar(c));
        }
        return out;
    }

    QString currentIndent;
    int i = 0;
    while (i < text.size()) {
        const int lineStart = i;
        while (i < text.size() && text[i] != QLatin1Char('\n') && text[i] != QLatin1Char('\r'))
            ++i;
        const QString line = text.mid(lineStart, i - lineStart);

        bool newline = false;
        if (i < text.size() && text[i] == QLatin1Char('\r')) {
            newline = true;
            ++i;
        }
        if (i < text.size() && text[i] == QLatin1Char('\n')) {
            newline = true;
            ++i;
        }

        const QString desired = leadingWhitespace(line);
        const QString content = line.mid(desired.size());

        if (desired == currentIndent) {
            // Editor already kept this indent.
        } else if (desired.startsWith(currentIndent)) {
            const QString extra = desired.mid(currentIndent.size());
            for (QChar c : extra)
                out.append(fromChar(c));
        } else if (currentIndent.startsWith(desired)) {
            const int backs = currentIndent.size() - desired.size();
            for (int b = 0; b < backs; ++b) {
                TypeAction back;
                back.kind = ActionKind::Backspace;
                out.append(back);
            }
        } else {
            for (int b = 0; b < currentIndent.size(); ++b) {
                TypeAction back;
                back.kind = ActionKind::Backspace;
                out.append(back);
            }
            for (QChar c : desired)
                out.append(fromChar(c));
        }

        for (QChar c : content)
            out.append(fromChar(c));

        if (newline) {
            out.append(fromChar(QLatin1Char('\n')));
            currentIndent = desired;
        }
    }
    return out;
}

QChar TypingEngine::neighborOf(QChar c) const
{
    static const QHash<QChar, QString> map = {
        {QLatin1Char('a'), QStringLiteral("sqwz")},
        {QLatin1Char('b'), QStringLiteral("vghn")},
        {QLatin1Char('c'), QStringLiteral("xdfv")},
        {QLatin1Char('d'), QStringLiteral("sfcerx")},
        {QLatin1Char('e'), QStringLiteral("wrsd3")},
        {QLatin1Char('f'), QStringLiteral("dgvcrt")},
        {QLatin1Char('g'), QStringLiteral("fhvtyb")},
        {QLatin1Char('h'), QStringLiteral("gjbnuy")},
        {QLatin1Char('i'), QStringLiteral("uojk8")},
        {QLatin1Char('j'), QStringLiteral("hknumi")},
        {QLatin1Char('k'), QStringLiteral("jlmoi,")},
        {QLatin1Char('l'), QStringLiteral("k;op.")},
        {QLatin1Char('m'), QStringLiteral("njk,")},
        {QLatin1Char('n'), QStringLiteral("bhjm")},
        {QLatin1Char('o'), QStringLiteral("iplk9")},
        {QLatin1Char('p'), QStringLiteral("ol0-[")},
        {QLatin1Char('q'), QStringLiteral("wa12")},
        {QLatin1Char('r'), QStringLiteral("etdf4")},
        {QLatin1Char('s'), QStringLiteral("awdxz")},
        {QLatin1Char('t'), QStringLiteral("ryfg5")},
        {QLatin1Char('u'), QStringLiteral("yihj7")},
        {QLatin1Char('v'), QStringLiteral("cfgb")},
        {QLatin1Char('w'), QStringLiteral("qeas2")},
        {QLatin1Char('x'), QStringLiteral("zsdc")},
        {QLatin1Char('y'), QStringLiteral("tugh6")},
        {QLatin1Char('z'), QStringLiteral("asx")},
        {QLatin1Char('1'), QStringLiteral("2q")},
        {QLatin1Char('2'), QStringLiteral("13qw")},
        {QLatin1Char('3'), QStringLiteral("24we")},
        {QLatin1Char('4'), QStringLiteral("35er")},
        {QLatin1Char('5'), QStringLiteral("46rt")},
        {QLatin1Char('6'), QStringLiteral("57ty")},
        {QLatin1Char('7'), QStringLiteral("68yu")},
        {QLatin1Char('8'), QStringLiteral("79ui")},
        {QLatin1Char('9'), QStringLiteral("80io")},
        {QLatin1Char('0'), QStringLiteral("9op")},
    };

    const bool upper = c.isUpper();
    const QChar key = c.toLower();
    QString options = map.value(key);
    if (options.isEmpty())
        options = QStringLiteral("asdfghjkl");

    QChar pick = options.at(uni(0, options.size() - 1));
    if (pick == key && options.size() > 1)
        pick = options.at(uni(0, options.size() - 1));
    if (upper && pick.isLetter())
        pick = pick.toUpper();
    return pick;
}
