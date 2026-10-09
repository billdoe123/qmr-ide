#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Msftedit.lib")
#pragma comment(lib, "shell32.lib")

constexpr UINT ID_EDITOR = 1001;
constexpr UINT ID_OUTPUT = 1002;
constexpr UINT ID_COMPILE = 1003;
constexpr UINT ID_OPEN = 1004;
constexpr UINT ID_SAVE = 1005;
constexpr UINT ID_NEW = 1006;
constexpr UINT ID_RUN = 1007;
constexpr UINT ID_COMPLETION = 1008;

struct IDEState {
    HWND hwnd = nullptr;
    HWND editor = nullptr;
    HWND output = nullptr;
    HWND completion = nullptr;
    std::wstring currentFile;
    std::wstring compilerPath = L"compiler.exe";
    bool completionVisible = false;
};

static const std::vector<std::wstring> kKeywords = {
    L"func", L"if", L"else", L"elif", L"while", L"end",
    L"return", L"break", L"continue", L"struct", L"extern",
    L"print", L"use", L"include", L"new", L"asm", L"call",
    L"load", L"store", L"Terminate", L"Insert_Newline", L"Begin",
    L"Entry_Point", L"int", L"char", L"str", L"string", L"String",
    L"void", L"bool", L"true", L"false", L"sizeof", L"input",
    L"Rand", L"if", L"while", L"return"
};

static const std::vector<std::wstring> kFunctions = {
    L"Print", L"RandomNumbers", L"GraceFulExit", L"ExitProcess",
    L"strlen", L"strcmp", L"memcpy", L"malloc", L"free",
    L"open", L"read", L"write", L"close"
};

static std::unordered_set<std::wstring> BuildKeywordSet() {
    std::unordered_set<std::wstring> set;
    for (const auto& k : kKeywords) set.insert(k);
    for (const auto& f : kFunctions) set.insert(f);
    return set;
}

static bool IsWordChar(wchar_t ch) {
    return (ch == L'_') || (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
           (ch >= L'0' && ch <= L'9');
}

static std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return s;
}

static void SetOutputText(HWND output, const std::wstring& text) {
    SetWindowTextW(output, text.c_str());
}

static std::wstring ReadEditorText(HWND editor) {
    int len = GetWindowTextLengthW(editor);
    if (len <= 0) return L"";

    std::wstring out(len, L'\0');
    GetWindowTextW(editor, out.data(), len + 1);
    return out;
}

static void SetEditorText(HWND editor, const std::wstring& text) {
    SetWindowTextW(editor, text.c_str());
    SendMessageW(editor, EM_SETSEL, 0, 0);
    SendMessageW(editor, EM_SCROLLCARET, 0, 0);
}

static void SetRangeFormat(HWND editor, int start, int len, COLORREF color, bool bold = false, bool italic = false, bool underline = false) {
    if (len <= 0 || start < 0) return;

    CHARRANGE cr = {start, start + len};
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));

    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.crTextColor = color;
    cf.dwEffects = 0;
    if (bold) cf.dwEffects |= CFE_BOLD;
    if (italic) cf.dwEffects |= CFE_ITALIC;
    if (underline) cf.dwEffects |= CFE_UNDERLINE;

    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

static void ApplyColorizedRange(HWND editor, int start, int len, COLORREF color, bool bold = false, bool italic = false, bool underline = false) {
    if (start < 0 || len <= 0) return;
    SetRangeFormat(editor, start, len, color, bold, italic, underline);
}

static void HighlightEditor(HWND editor) {
    if (!editor) return;

    std::wstring text = ReadEditorText(editor);
    if (text.empty()) return;

    static const std::unordered_set<std::wstring> keywordSet = BuildKeywordSet();

    SendMessageW(editor, WM_SETREDRAW, FALSE, 0);

    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC;
    cf.crTextColor = RGB(220, 220, 220);
    cf.dwEffects = 0;
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));

    int i = 0;
    while (i < static_cast<int>(text.size())) {
        wchar_t ch = text[i];

        if (ch == L'/' && i + 1 < static_cast<int>(text.size()) && text[i + 1] == L'/') {
            int start = i;
            i += 2;
            while (i < static_cast<int>(text.size()) && text[i] != L'\n') ++i;
            ApplyColorizedRange(editor, start, i - start, RGB(90, 160, 110), false, true);
            continue;
        }

        if (ch == L'@') {
            int start = i;
            ++i;
            while (i < static_cast<int>(text.size()) && text[i] != L'@') ++i;
            if (i < static_cast<int>(text.size())) ++i;
            ApplyColorizedRange(editor, start, i - start, RGB(90, 160, 110), false, true);
            continue;
        }

        if (ch == L'"') {
            int start = i;
            ++i;
            while (i < static_cast<int>(text.size())) {
                if (text[i] == L'\\' && i + 1 < static_cast<int>(text.size())) {
                    i += 2;
                    continue;
                }
                if (text[i] == L'"') {
                    ++i;
                    break;
                }
                ++i;
            }
            ApplyColorizedRange(editor, start, i - start, RGB(255, 176, 112), false, false);
            continue;
        }

        if ((ch >= L'0' && ch <= L'9') || (ch == L'-' && i + 1 < static_cast<int>(text.size()) && (text[i + 1] >= L'0' && text[i + 1] <= L'9'))) {
            int start = i;
            ++i;
            while (i < static_cast<int>(text.size()) && ((text[i] >= L'0' && text[i] <= L'9') || text[i] == L'x' || text[i] == L'X' || (text[i] >= L'a' && text[i] <= L'f') || (text[i] >= L'A' && text[i] <= L'F'))) ++i;
            ApplyColorizedRange(editor, start, i - start, RGB(97, 214, 255), false, false);
            continue;
        }

        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || ch == L'_') {
            int start = i;
            ++i;
            while (i < static_cast<int>(text.size()) && IsWordChar(text[i])) ++i;
            std::wstring word = text.substr(start, i - start);
            auto lower = ToLower(word);
            bool isKeyword = keywordSet.find(lower) != keywordSet.end();
            ApplyColorizedRange(editor, start, i - start, isKeyword ? RGB(130, 170, 255) : RGB(220, 220, 220), isKeyword, false, false);
            continue;
        }

        if (ch == L'=' || ch == L'+' || ch == L'-' || ch == L'*' || ch == L'/' || ch == L'%' || ch == L'<' || ch == L'>' || ch == L'!' || ch == L'&' || ch == L'|' || ch == L'^' || ch == L'~' || ch == L'[' || ch == L']' || ch == L'(' || ch == L')' || ch == L'{' || ch == L'}' || ch == L';' || ch == L':' || ch == L',' || ch == L'.') {
            ApplyColorizedRange(editor, i, 1, RGB(255, 214, 102), false, false);
            ++i;
            continue;
        }

        ++i;
    }

    SendMessageW(editor, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(editor, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

static std::wstring GetWordAtCaret(HWND editor) {
    DWORD pos = static_cast<DWORD>(SendMessageW(editor, EM_GETSEL, 0, 0));
    int caret = LOWORD(pos);

    std::wstring text = ReadEditorText(editor);
    int start = caret;
    int end = caret;

    while (start > 0 && IsWordChar(text[start - 1])) --start;
    while (end < static_cast<int>(text.size()) && IsWordChar(text[end])) ++end;

    return text.substr(start, end - start);
}

static void ShowCompletionList(HWND hwnd, IDEState* state, const std::wstring& prefix) {
    if (!state->completion) return;

    std::vector<std::wstring> matches;
    std::wstring lowerPrefix = ToLower(prefix);
    for (const auto& kw : kKeywords) {
        std::wstring lower = ToLower(kw);
        if (lowerPrefix.empty() || lower.find(lowerPrefix) == 0) {
            matches.push_back(kw);
        }
    }
    for (const auto& fn : kFunctions) {
        std::wstring lower = ToLower(fn);
        if (lowerPrefix.empty() || lower.find(lowerPrefix) == 0) {
            matches.push_back(fn);
        }
    }

    SendMessageW(state->completion, LB_RESETCONTENT, 0, 0);
    for (const auto& item : matches) {
        SendMessageW(state->completion, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }

    if (matches.empty()) {
        ShowWindow(state->completion, SW_HIDE);
        state->completionVisible = false;
        return;
    }

    RECT rc = {};
    GetWindowRect(state->editor, &rc);
    POINT p = {rc.left, rc.top};
    ScreenToClient(hwnd, &p);

    SetWindowPos(state->completion, HWND_TOP, p.x + 12, p.y + 120, 220, 160, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(state->completion, SW_SHOW);
    state->completionVisible = true;
}

static void InsertCompletion(HWND editor, const std::wstring& selectedText) {
    if (selectedText.empty()) return;

    DWORD sel = static_cast<DWORD>(SendMessageW(editor, EM_GETSEL, 0, 0));
    int caret = LOWORD(sel);

    std::wstring text = ReadEditorText(editor);
    int start = caret;
    while (start > 0 && IsWordChar(text[start - 1])) --start;
    int end = caret;
    while (end < static_cast<int>(text.size()) && IsWordChar(text[end])) ++end;

    std::wstring before = text.substr(0, start);
    std::wstring after = text.substr(end);
    std::wstring newText = before + selectedText + after;
    SetEditorText(editor, newText);
    int newCaret = static_cast<int>(before.size() + selectedText.size());
    SendMessageW(editor, EM_SETSEL, newCaret, newCaret);
}

static bool SaveCurrentFile(IDEState* state) {
    if (state->currentFile.empty()) {
        wchar_t path[MAX_PATH] = {0};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = state->hwnd;
        ofn.lpstrFilter = L"QMR Files (*.qmr)\0*.qmr\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;

        if (!GetSaveFileNameW(&ofn)) return false;
        state->currentFile = path;
    }

    std::wstring content = ReadEditorText(state->editor);
    std::wofstream out(state->currentFile, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(content.c_str(), static_cast<std::streamsize>(content.size()));
    out.close();
    return true;
}

static bool OpenFileIntoEditor(IDEState* state) {
    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = state->hwnd;
    ofn.lpstrFilter = L"QMR Files (*.qmr)\0*.qmr\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return false;

    std::wifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    std::wstring content((std::istreambuf_iterator<wchar_t>(in)), std::istreambuf_iterator<wchar_t>());
    state->currentFile = path;
    SetEditorText(state->editor, content);
    HighlightEditor(state->editor);
    return true;
}

static bool CompileCurrentFile(IDEState* state) {
    if (state->currentFile.empty()) {
        if (!SaveCurrentFile(state)) {
            SetOutputText(state->output, L"Save the file before compiling.");
            return false;
        }
    }

    std::wstring sourceFile = state->currentFile;
    std::wstring outputFile = sourceFile + L".asm";
    std::wstring logFile = sourceFile + L".compile.log";

    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    const size_t slash = exeDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exeDir = exeDir.substr(0, slash + 1);

    std::wstring compiler = exeDir + state->compilerPath;
    std::wstring command = L"cmd /C \"\"" + compiler + L"\" \"" + sourceFile + L"\" \"" + outputFile + L"\" > \"" + logFile + L"\" 2>&1\"";

    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::wstring err = L"Failed to start compiler: " + state->compilerPath + L"\r\nCheck that the compiler is in the same folder as the IDE.";
        SetOutputText(state->output, err);
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::wifstream log(logFile, std::ios::binary);
    std::wstring result;
    if (log.is_open()) {
        result.assign((std::istreambuf_iterator<wchar_t>(log)), std::istreambuf_iterator<wchar_t>());
    } else {
        result = L"Compilation started but the log file could not be read.";
    }

    SetOutputText(state->output, result.empty() ? L"Compilation succeeded." : result);
    return true;
}

static void NewFile(IDEState* state) {
    state->currentFile.clear();
    SetEditorText(state->editor, L"");
    SetOutputText(state->output, L"New file created.");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    IDEState* state = reinterpret_cast<IDEState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            state = new IDEState();
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            HWND toolbar = CreateWindowExW(0, L"BUTTON", L"New", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 8, 8, 80, 30, hwnd, reinterpret_cast<HMENU>(ID_NEW), GetModuleHandleW(nullptr), nullptr);
            SetWindowTextW(CreateWindowExW(0, L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 96, 8, 80, 30, hwnd, reinterpret_cast<HMENU>(ID_OPEN), GetModuleHandleW(nullptr), nullptr), L"Open");
            CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 184, 8, 80, 30, hwnd, reinterpret_cast<HMENU>(ID_SAVE), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"Compile", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 272, 8, 90, 30, hwnd, reinterpret_cast<HMENU>(ID_COMPILE), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"Run", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 370, 8, 90, 30, hwnd, reinterpret_cast<HMENU>(ID_RUN), GetModuleHandleW(nullptr), nullptr);

            state->editor = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                8, 48, 760, 400, hwnd, reinterpret_cast<HMENU>(ID_EDITOR), GetModuleHandleW(nullptr), nullptr);

            if (state->editor) {
                SendMessageW(state->editor, EM_SETBKGNDCOLOR, 0, RGB(20, 23, 30));
                SendMessageW(state->editor, EM_SETEVENTMASK, 0, ENM_CHANGE);
                CHARFORMAT2W cf = {};
                cf.cbSize = sizeof(cf);
                cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_BOLD;
                cf.crTextColor = RGB(220, 220, 220);
                cf.yHeight = 200;
                wcscpy_s(cf.szFaceName, L"Consolas");
                SendMessageW(state->editor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
            }

            state->output = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER,
                8, 456, 760, 120, hwnd, reinterpret_cast<HMENU>(ID_OUTPUT), GetModuleHandleW(nullptr), nullptr);

            state->completion = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS | LBS_NOSEL,
                0, 0, 220, 160, hwnd, reinterpret_cast<HMENU>(ID_COMPLETION), GetModuleHandleW(nullptr), nullptr);
            ShowWindow(state->completion, SW_HIDE);
            state->completionVisible = false;

            SetOutputText(state->output, L"QMR IDE ready. Press Ctrl+Space for auto-complete.");
            return 0;
        }

        case WM_SIZE: {
            if (!state) return 0;
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (state->editor) SetWindowPos(state->editor, nullptr, 8, 48, w - 24, h - 190, SWP_NOZORDER);
            if (state->output) SetWindowPos(state->output, nullptr, 8, h - 132, w - 24, 120, SWP_NOZORDER);
            return 0;
        }

        case WM_COMMAND: {
            if (!state) return 0;
            switch (LOWORD(wParam)) {
                case ID_NEW:
                    NewFile(state);
                    break;
                case ID_OPEN:
                    OpenFileIntoEditor(state);
                    break;
                case ID_SAVE:
                    SaveCurrentFile(state);
                    break;
                case ID_COMPILE:
                    CompileCurrentFile(state);
                    break;
                case ID_RUN: {
                    std::wstring msg = L"Run command is not yet wired to a QMR VM. Use Compile to generate the assembly file.";
                    SetOutputText(state->output, msg);
                    break;
                }
                case ID_COMPLETION:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        int idx = (int)SendMessageW(state->completion, LB_GETCURSEL, 0, 0);
                        if (idx != LB_ERR) {
                            wchar_t buf[256] = {0};
                            SendMessageW(state->completion, LB_GETTEXT, idx, reinterpret_cast<LPARAM>(buf));
                            InsertCompletion(state->editor, buf);
                            ShowWindow(state->completion, SW_HIDE);
                            state->completionVisible = false;
                        }
                    }
                    break;
            }
            return 0;
        }

        case WM_NOTIFY: {
            if (!state) return 0;
            if (((LPNMHDR)lParam)->code == EN_CHANGE && ((LPNMHDR)lParam)->hwndFrom == state->editor) {
                HighlightEditor(state->editor);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (!state) return 0;
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == VK_SPACE)) {
                std::wstring word = GetWordAtCaret(state->editor);
                ShowCompletionList(hwnd, state, word);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'S')) {
                SaveCurrentFile(state);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'O')) {
                OpenFileIntoEditor(state);
                return 0;
            }
            if (wParam == VK_F5) {
                CompileCurrentFile(state);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_LBUTTONDOWN: {
            if (state && state->completionVisible) {
                ShowWindow(state->completion, SW_HIDE);
                state->completionVisible = false;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_DESTROY: {
            delete state;
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int /*nCmdShow*/) {
    INITCOMMONCONTROLSEX iccx = {sizeof(iccx), ICC_STANDARD_CLASSES | ICC_USEREX_CLASSES};
    InitCommonControlsEx(&iccx);

    LoadLibraryW(L"Msftedit.dll");

    const wchar_t* className = L"QMRIDEWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"QMR Win32 IDE",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        840,
        650,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
