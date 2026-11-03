#include "typingengine.h"

#include <QHash>
#include <QRandomGenerator>
#include <QVector>

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

bool isExtendedKey(WORD vk)
{
    switch (vk) {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_RCONTROL:
    case VK_RMENU:
        return true;
    default:
        return false;
    }
}

WORD scanOf(WORD vk)
{
    WORD scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    if (scan)
        return scan;
    switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
        return 0x2A;
    case VK_RSHIFT:
        return 0x36;
    case VK_CONTROL:
    case VK_LCONTROL:
        return 0x1D;
    case VK_MENU:
    case VK_LMENU:
        return 0x38;
    default:
        return 0;
    }
}

LPARAM keyLParam(WORD vk, bool down)
{
    const WORD scan = scanOf(vk);
    LPARAM lp = 1 | (static_cast<LPARAM>(scan) << 16);
    if (isExtendedKey(vk))
        lp |= (1L << 24);
    if (!down)
        lp |= (1L << 30) | (1L << 31);
    return lp;
}

HWND focusedControl(HWND root)
{
    if (!root || !IsWindow(root))
        return nullptr;

    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    const DWORD tid = GetWindowThreadProcessId(root, nullptr);
    if (tid && GetGUIThreadInfo(tid, &info)) {
        if (info.hwndFocus && IsWindow(info.hwndFocus))
            return info.hwndFocus;
        if (info.hwndActive && IsWindow(info.hwndActive))
            return info.hwndActive;
    }
    return root;
}

bool isOwnedByCurrentProcess(HWND hwnd)
{
    if (!hwnd)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

HWND nextForeignWindow(HWND start)
{
    HWND w = start;
    while (w) {
        w = GetWindow(w, GW_HWNDNEXT);
        if (!w)
            break;
        if (!IsWindowVisible(w) || !IsWindowEnabled(w))
            continue;
        if (GetWindow(w, GW_OWNER))
            continue;
        if (isOwnedByCurrentProcess(w))
            continue;
        const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        if (ex & WS_EX_TOOLWINDOW)
            continue;
        HWND root = GetAncestor(w, GA_ROOT);
        return root ? root : w;
    }
    return nullptr;
}

HWND captureForegroundTarget()
{
    HWND fg = GetForegroundWindow();
    if (fg && !isOwnedByCurrentProcess(fg)) {
        HWND root = GetAncestor(fg, GA_ROOT);
        return root ? root : fg;
    }
    HWND foreign = nextForeignWindow(fg);
    if (foreign)
        return foreign;
    return fg;
}

HWND resolveControl(HWND root, HWND stored)
{
    if (stored && IsWindow(stored))
        return stored;
    HWND focus = focusedControl(root);
    if (focus && IsWindow(focus))
        return focus;
    return root;
}

bool windowIsUnder(HWND hwnd, HWND root)
{
    while (hwnd) {
        if (hwnd == root)
            return true;
        hwnd = GetParent(hwnd);
    }
    return false;
}

bool sameProcessWindow(HWND a, HWND b)
{
    if (!a || !b)
        return false;
    DWORD pa = 0;
    DWORD pb = 0;
    GetWindowThreadProcessId(a, &pa);
    GetWindowThreadProcessId(b, &pb);
    return pa != 0 && pa == pb;
}

bool targetIsForeground(HWND root)
{
    if (!root || !IsWindow(root))
        return false;
    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;
    if (fg == root || windowIsUnder(fg, root) || sameProcessWindow(fg, root))
        return true;
    HWND fgRoot = GetAncestor(fg, GA_ROOT);
    return fgRoot && (fgRoot == root || sameProcessWindow(fgRoot, root));
}

void sendInputKey(WORD vk, bool down)
{
    INPUT ip{};
    ip.type = INPUT_KEYBOARD;
    ip.ki.wVk = vk;
    ip.ki.wScan = scanOf(vk);
    ip.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if (isExtendedKey(vk))
        ip.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &ip, sizeof(INPUT));
}

void sendInputScan(WORD scan, bool down, bool extended = false)
{
    INPUT ip{};
    ip.type = INPUT_KEYBOARD;
    ip.ki.wScan = scan;
    ip.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    if (extended)
        ip.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &ip, sizeof(INPUT));
}

void sendInputUnicode(ushort code)
{
    INPUT ip[2]{};
    ip[0].type = INPUT_KEYBOARD;
    ip[0].ki.wVk = 0;
    ip[0].ki.wScan = code;
    ip[0].ki.dwFlags = KEYEVENTF_UNICODE;
    ip[1] = ip[0];
    ip[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, ip, sizeof(INPUT));
}

void postKey(HWND hwnd, WORD vk, bool down)
{
    const UINT msg = down ? WM_KEYDOWN : WM_KEYUP;
    PostMessageW(hwnd, msg, vk, keyLParam(vk, down));
}

bool classLooksEditable(HWND hwnd)
{
    if (!hwnd)
        return false;
    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, 256);
    const QString c = QString::fromWCharArray(cls).toLower();
    return c.contains(QLatin1String("edit"))
        || c.contains(QLatin1String("scintilla"));
}

struct EditSearch {
    HWND found = nullptr;
};

BOOL CALLBACK findEditChild(HWND child, LPARAM lp)
{
    if (!IsWindowVisible(child))
        return TRUE;
    if (classLooksEditable(child)) {
        reinterpret_cast<EditSearch *>(lp)->found = child;
        return FALSE;
    }
    return TRUE;
}

HWND bestEditableControl(HWND root, HWND hint)
{
    if (classLooksEditable(hint))
        return hint;
    HWND walk = hint;
    while (walk && walk != root) {
        if (classLooksEditable(walk))
            return walk;
        walk = GetParent(walk);
    }
    if (classLooksEditable(root))
        return root;
    EditSearch search;
    if (root)
        EnumChildWindows(root, findEditChild, reinterpret_cast<LPARAM>(&search));
    if (search.found)
        return search.found;
    return hint ? hint : root;
}

BOOL CALLBACK collectChildWindows(HWND child, LPARAM lp)
{
    reinterpret_cast<QVector<HWND> *>(lp)->append(child);
    return TRUE;
}

bool tryReplaceSel(HWND hwnd, QChar ch)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    const int before = static_cast<int>(SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));
    const wchar_t text[2] = { static_cast<wchar_t>(ch.unicode()), 0 };
    DWORD_PTR dummy = 0;
    SendMessageTimeoutW(hwnd, EM_REPLACESEL, TRUE,
                        reinterpret_cast<LPARAM>(text),
                        SMTO_ABORTIFHUNG | SMTO_NORMAL, 80, &dummy);
    const int after = static_cast<int>(SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));
    return after > before;
}

bool tryPasteChar(HWND hwnd, QChar ch)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    if (!OpenClipboard(nullptr))
        return false;

    QString saved;
    if (HANDLE existing = GetClipboardData(CF_UNICODETEXT)) {
        if (auto *p = static_cast<wchar_t *>(GlobalLock(existing))) {
            saved = QString::fromWCharArray(p);
            GlobalUnlock(existing);
        }
    }

    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * 2);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    auto *p = static_cast<wchar_t *>(GlobalLock(mem));
    p[0] = static_cast<wchar_t>(ch.unicode());
    p[1] = 0;
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();

    const int before = static_cast<int>(SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));
    DWORD_PTR dummy = 0;
    SendMessageTimeoutW(hwnd, WM_PASTE, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 80, &dummy);
    const int after = static_cast<int>(SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));

    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        if (!saved.isEmpty()) {
            const int n = saved.size() + 1;
            HGLOBAL restore = GlobalAlloc(GMEM_MOVEABLE, sizeof(wchar_t) * n);
            if (restore) {
                auto *out = static_cast<wchar_t *>(GlobalLock(restore));
                saved.toWCharArray(out);
                out[saved.size()] = 0;
                GlobalUnlock(restore);
                SetClipboardData(CF_UNICODETEXT, restore);
            }
        }
        CloseClipboard();
    }
    return after > before;
}

void insertExactChar(HWND root, HWND hint, QChar ch)
{
    QVector<HWND> candidates;
    auto add = [&candidates](HWND h) {
        if (h && IsWindow(h) && !candidates.contains(h))
            candidates.append(h);
    };
    add(hint);
    add(focusedControl(root));
    add(root);
    if (root)
        EnumChildWindows(root, collectChildWindows, reinterpret_cast<LPARAM>(&candidates));

    for (HWND hwnd : candidates) {
        if (tryReplaceSel(hwnd, ch))
            return;
    }
    for (HWND hwnd : candidates) {
        if (tryPasteChar(hwnd, ch))
            return;
    }
}

void deliverCharMessage(HWND hwnd, ushort code)
{
    if (!hwnd || !IsWindow(hwnd))
        return;
    DWORD_PTR dummy = 0;
    SendMessageTimeoutW(hwnd, WM_CHAR, code, 1,
                        SMTO_ABORTIFHUNG | SMTO_NORMAL, 50, &dummy);
}

QString processImagePath(HWND hwnd)
{
    if (!hwnd)
        return {};
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    QString result;
    if (QueryFullProcessImageNameW(process, 0, path, &size))
        result = QString::fromWCharArray(path).toLower();
    CloseHandle(process);
    return result;
}

bool looksLikeRemoteDesktop(HWND hwnd)
{
    if (!hwnd)
        return false;

    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, 512);
    const QString t = QString::fromWCharArray(title).toLower();
    if (t.contains(QLatin1String("anydesk"))
        || t.contains(QLatin1String("jump desktop"))
        || t.contains(QLatin1String("jumpdesk"))
        || t.contains(QLatin1String("jump connect"))
        || t.contains(QLatin1String("teamviewer"))
        || t.contains(QLatin1String("rustdesk"))
        || t.contains(QLatin1String("remote desktop"))
        || t.contains(QLatin1String("splashtop"))
        || t.contains(QLatin1String("todesk"))) {
        return true;
    }

    const QString p = processImagePath(hwnd);
    return p.contains(QLatin1String("anydesk"))
        || p.contains(QLatin1String("jumpdesktop"))
        || p.contains(QLatin1String("jumpconnect"))
        || p.contains(QLatin1String("jumpdesk"))
        || p.contains(QLatin1String("jumpclient"))
        || p.contains(QLatin1String("teamviewer"))
        || p.contains(QLatin1String("tv_w32"))
        || p.contains(QLatin1String("rustdesk"))
        || p.contains(QLatin1String("mstsc"))
        || p.contains(QLatin1String("msrdc"))
        || p.contains(QLatin1String("splashtop"))
        || p.contains(QLatin1String("todesk"));
}

bool isRemoteTypingContext(HWND root)
{
    HWND fg = GetForegroundWindow();
    if (looksLikeRemoteDesktop(root) || looksLikeRemoteDesktop(fg))
        return true;
    if (root && fg && sameProcessWindow(root, fg) && looksLikeRemoteDesktop(root))
        return true;
    return false;
}

bool useHardwareInjection(HWND root)
{
    return root && IsWindow(root) && (isRemoteTypingContext(root) || targetIsForeground(root));
}

void appendKeyEvent(QVector<INPUT> *out, WORD vk, WORD scan, bool down, bool extended)
{
    INPUT ip{};
    ip.type = INPUT_KEYBOARD;
    ip.ki.wVk = vk;
    ip.ki.wScan = scan ? scan : scanOf(vk);
    ip.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if (extended)
        ip.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    out->append(ip);
}

void sendPhysicalCharForeground(QChar ch)
{
    const SHORT mapped = VkKeyScanW(ch.unicode());
    if (mapped == -1) {
        sendInputUnicode(ch.unicode());
        return;
    }

    const WORD vk = LOBYTE(mapped);
    const BYTE mods = HIBYTE(mapped);
    bool shift = (mods & 1) != 0;
    const bool ctrl = (mods & 2) != 0;
    const bool alt = (mods & 4) != 0;
    const bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;

    if (ch.isLetter())
        shift = ch.isUpper() ? !caps : caps;

    const WORD scan = scanOf(vk);
    const bool extended = isExtendedKey(vk);
    QVector<INPUT> batch;
    batch.reserve(8);

    if (shift)
        appendKeyEvent(&batch, VK_LSHIFT, 0x2A, true, false);
    if (ctrl)
        appendKeyEvent(&batch, VK_LCONTROL, 0x1D, true, false);
    if (alt)
        appendKeyEvent(&batch, VK_LMENU, 0x38, true, false);

    appendKeyEvent(&batch, vk, scan, true, extended);
    appendKeyEvent(&batch, vk, scan, false, extended);

    if (alt)
        appendKeyEvent(&batch, VK_LMENU, 0x38, false, false);
    if (ctrl)
        appendKeyEvent(&batch, VK_LCONTROL, 0x1D, false, false);
    if (shift)
        appendKeyEvent(&batch, VK_LSHIFT, 0x2A, false, false);

    if (!batch.isEmpty())
        SendInput(static_cast<UINT>(batch.size()), batch.data(), sizeof(INPUT));
}

void sendVirtualForeground(WORD vk)
{
    sendInputKey(vk, true);
    sendInputKey(vk, false);
}

void sendVirtualBackground(HWND hwnd, WORD vk)
{
    if (vk == VK_RETURN) {
        deliverCharMessage(hwnd, '\r');
        return;
    }
    if (vk == VK_TAB) {
        deliverCharMessage(hwnd, '\t');
        return;
    }
    if (vk == VK_BACK) {
        deliverCharMessage(hwnd, VK_BACK);
        return;
    }
    postKey(hwnd, vk, true);
    postKey(hwnd, vk, false);
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

void DelayService::setWpm(int wpm)
{
    targetWpm = qBound(15, wpm, 120);
}

int DelayService::scaled(int ms) const
{
    const double factor = 45.0 / double(qBound(15, targetWpm, 120));
    return clampMs(int(ms * factor + 0.5), 8, 20000);
}

int DelayService::forAction(const TypeAction &action)
{
    const CharKind kind = kindOf(action);

    if (kind != CharKind::Lower && kind != CharKind::Upper)
        inWord = false;

    // Rare long pause — thinking about the next keystroke. Not scaled with WPM.
    if (kind != CharKind::Backspace && chance(3))
        return uni(5000, 10000);

    switch (kind) {
    case CharKind::Lower: {
        if (!inWord) {
            wordBaseMs = uni(52, 90);
            inWord = true;
        }
        return scaled(clampMs(wordBaseMs + uni(-14, 18), 38, 120));
    }
    case CharKind::Upper:
        inWord = true;
        return scaled(uni(95, 180));
    case CharKind::Digit:
        return scaled(uni(80, 165));
    case CharKind::Special:
        return scaled(uni(160, 380));
    case CharKind::Period:
        if (chance(80))
            return uni(5000, 10000);
        return scaled(uni(480, 1400));
    case CharKind::Space:
        return scaled(uni(90, 220));
    case CharKind::Tab:
        return scaled(uni(250, 600));
    case CharKind::Enter:
        if (chance(45))
            return uni(5000, 10000);
        return scaled(uni(380, 1100));
    case CharKind::Backspace:
        return scaled(uni(70, 140));
    }
    return scaled(uni(80, 140));
}

int DelayService::forWrongKey()
{
    return scaled(uni(55, 130));
}

int DelayService::realizePause() const
{
    return scaled(uni(280, 720));
}

int DelayService::rapidBackspace() const
{
    return scaled(uni(42, 88));
}

TypingEngine::TypingEngine(QObject *parent)
    : QObject(parent)
    , timer(new QTimer(this))
    , watchTimer(new QTimer(this))
{
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &TypingEngine::onTick);
    watchTimer->setInterval(80);
    connect(watchTimer, &QTimer::timeout, this, &TypingEngine::watchForeground);
    watchTimer->start();
    watchForeground();
}

void TypingEngine::setWpm(int wpm)
{
    delays.setWpm(wpm);
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
    freezeTarget();
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
    if (!targetRoot)
        freezeTarget();
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
    targetRoot = nullptr;
    targetControl = nullptr;
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

void TypingEngine::watchForeground()
{
#ifdef Q_OS_WIN
    if (running)
        return;
    HWND fg = GetForegroundWindow();
    if (!fg || isOwnedByCurrentProcess(fg))
        return;
    HWND root = GetAncestor(fg, GA_ROOT);
    if (!root)
        root = fg;
    lastRoot = static_cast<void *>(root);
    HWND control = focusedControl(root);
    lastControl = static_cast<void *>(control ? control : root);
#endif
}

void TypingEngine::freezeTarget()
{
#ifdef Q_OS_WIN
    HWND root = static_cast<HWND>(lastRoot);
    HWND control = static_cast<HWND>(lastControl);
    if (!root || !IsWindow(root)) {
        root = captureForegroundTarget();
        control = focusedControl(root);
    }
    if (root && isOwnedByCurrentProcess(root)) {
        root = nextForeignWindow(root);
        control = focusedControl(root);
    }
    if (!control)
        control = focusedControl(root);
    control = bestEditableControl(root, control);
    targetRoot = static_cast<void *>(root);
    targetControl = static_cast<void *>(control ? control : root);
#endif
}

void TypingEngine::abortTypoCleanup()
{
    if (toDelete > 0
        && (phase == Phase::TypoInsert
            || phase == Phase::TypoContinue
            || phase == Phase::TypoRealize
            || phase == Phase::TypoDelete)) {
        for (int i = 0; i < toDelete; ++i)
            sendVirtualKey(VK_BACK);
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
        sendVirtualKey(VK_BACK);
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
        sendVirtualKey(VK_RETURN);
        break;
    case ActionKind::Tab:
        sendVirtualKey(VK_TAB);
        break;
    case ActionKind::Backspace:
        sendVirtualKey(VK_BACK);
        break;
    case ActionKind::Char:
        if (action.ch == QLatin1Char('\t'))
            sendVirtualKey(VK_TAB);
        else
            sendChar(action.ch);
        break;
    }
}

void TypingEngine::sendChar(QChar ch)
{
#ifdef Q_OS_WIN
    HWND root = static_cast<HWND>(targetRoot);
    HWND hwnd = bestEditableControl(root, resolveControl(root, static_cast<HWND>(targetControl)));
    if (isRemoteTypingContext(root) || (root && !isOwnedByCurrentProcess(root) && targetIsForeground(root))) {
        sendPhysicalCharForeground(ch);
        return;
    }
    insertExactChar(root, hwnd, ch);
#else
    Q_UNUSED(ch);
#endif
}

void TypingEngine::sendVirtualKey(quint16 vk)
{
#ifdef Q_OS_WIN
    const WORD key = static_cast<WORD>(vk);
    HWND root = static_cast<HWND>(targetRoot);
    HWND hwnd = bestEditableControl(root, resolveControl(root, static_cast<HWND>(targetControl)));
    if (isRemoteTypingContext(root) || useHardwareInjection(root)) {
        sendVirtualForeground(key);
        return;
    }
    if (hwnd && !isOwnedByCurrentProcess(hwnd)) {
        sendVirtualBackground(hwnd, key);
        return;
    }
#else
    Q_UNUSED(vk);
#endif
}

void TypingEngine::sendWrongKey()
{
    QChar seed = QLatin1Char('a');
    if (index < actions.size() && actions[index].kind == ActionKind::Char)
        seed = actions[index].ch;
    sendChar(neighborOf(seed));
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
