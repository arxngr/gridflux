// win32.c — GridFlux Launcher
//
// Runs as asInvoker (no elevation).
// Supports --install-task to silently create a Task Scheduler entry so
// GridFlux auto-starts at logon elevated without a UAC prompt.
// Auto-restarts gridflux.exe on crash; stops on clean shutdown (exit 0).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <sddl.h>
// clang-format on

#include <stdio.h>
#include <wchar.h>

#define MUTEX_NAME L"GridFluxLauncherMutex"
#define EXE_NAME L"gridflux.exe"
#define TASK_NAME L"GridFlux"
#define RESTART_DELAY 2000

static BOOL
is_process_running (const wchar_t *exe_name)
{
    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return FALSE;

    PROCESSENTRY32W pe = { .dwSize = sizeof (pe) };
    BOOL found = FALSE;

    if (Process32FirstW (snap, &pe))
    {
        do
        {
            if (_wcsicmp (pe.szExeFile, exe_name) == 0)
            {
                found = TRUE;
                break;
            }
        } while (Process32NextW (snap, &pe));
    }

    CloseHandle (snap);
    return found;
}

static void
get_self_dir (wchar_t *buf, DWORD buf_len)
{
    GetModuleFileNameW (NULL, buf, buf_len);
    wchar_t *last_sep = wcsrchr (buf, L'\\');
    if (last_sep)
        *(last_sep + 1) = L'\0';
}

static BOOL
is_elevated (void)
{
    BOOL elevated = FALSE;
    HANDLE token = NULL;
    if (OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION elev = { 0 };
        DWORD size = 0;
        if (GetTokenInformation (token, TokenElevation, &elev, sizeof (elev), &size))
            elevated = elev.TokenIsElevated;
        CloseHandle (token);
    }
    return elevated;
}

static HANDLE
launch_elevated (const wchar_t *exe_path, const wchar_t *working_dir)
{
    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof (sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.lpDirectory = working_dir;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW (&sei))
        return NULL;

    return sei.hProcess;
}

static HANDLE
launch_same_level (const wchar_t *exe_path, const wchar_t *working_dir)
{
    wchar_t cmd[MAX_PATH + 4] = { 0 };
    _snwprintf (cmd, MAX_PATH + 4, L"\"%s\"", exe_path);

    STARTUPINFOW si = { .cb = sizeof (si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL ok = CreateProcessW (exe_path, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                              working_dir, &si, &pi);
    if (!ok)
        return NULL;

    CloseHandle (pi.hThread);
    return pi.hProcess;
}

// Escape XML metacharacters in `in` into `out`. Prevents a path containing
// '&', '<', '>', quotes from breaking (or injecting into) the task XML.
// Returns FALSE if the escaped result would not fit in `out`.
static BOOL
xml_escape_w (const wchar_t *in, wchar_t *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; in[i] != L'\0'; i++)
    {
        const wchar_t *rep = NULL;
        switch (in[i])
        {
        case L'&': rep = L"&amp;"; break;
        case L'<': rep = L"&lt;"; break;
        case L'>': rep = L"&gt;"; break;
        case L'"': rep = L"&quot;"; break;
        case L'\'': rep = L"&apos;"; break;
        }
        size_t rl = rep ? wcslen (rep) : 1;
        if (o + rl >= out_len)
            return FALSE;
        if (rep)
            wmemcpy (out + o, rep, rl);
        else
            out[o] = in[i];
        o += rl;
    }
    out[o] = L'\0';
    return TRUE;
}

// Build a SECURITY_ATTRIBUTES whose DACL grants access only to the file owner
// (current user), SYSTEM and Administrators. Caller LocalFrees the descriptor.
static BOOL
make_owner_only_sa (SECURITY_ATTRIBUTES *sa)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW (
            L"D:(A;;GA;;;OW)(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &sd, NULL))
        return FALSE;
    sa->nLength = sizeof (*sa);
    sa->lpSecurityDescriptor = sd;
    sa->bInheritHandle = FALSE;
    return TRUE;
}

// Write the scheduled-task XML (UTF-16LE + BOM) to xml_path with a restrictive
// DACL so a non-privileged user cannot tamper with it before schtasks reads it.
// launcher_path is XML-escaped before embedding. Returns FALSE on any failure.
static BOOL
write_task_xml (const wchar_t *xml_path, const wchar_t *launcher_path)
{
    wchar_t esc_path[MAX_PATH * 6];
    if (!xml_escape_w (launcher_path, esc_path, MAX_PATH * 6))
        return FALSE;

    SECURITY_ATTRIBUTES sa = { 0 };
    if (!make_owner_only_sa (&sa))
        return FALSE;

    // Drop any pre-existing file, then create with CREATE_NEW so our restrictive
    // DACL is actually applied (it is ignored when opening an existing file) and
    // creation fails closed if an attacker races to re-plant the file.
    DeleteFileW (xml_path);
    HANDLE h = CreateFileW (xml_path, GENERIC_WRITE, 0, &sa, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    LocalFree (sa.lpSecurityDescriptor);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    wchar_t xml[2048];
    int n = _snwprintf (
        xml, 2048,
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        L"<Task version=\"1.2\" "
        L"xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        L"  <Triggers>\n"
        L"    <LogonTrigger><Enabled>true</Enabled></LogonTrigger>\n"
        L"  </Triggers>\n"
        L"  <Principals>\n"
        L"    <Principal id=\"Author\">\n"
        L"      <GroupId>S-1-5-32-545</GroupId>\n"
        L"      <RunLevel>HighestAvailable</RunLevel>\n"
        L"    </Principal>\n"
        L"  </Principals>\n"
        L"  <Settings>\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
        L"  </Settings>\n"
        L"  <Actions Context=\"Author\">\n"
        L"    <Exec><Command>%s</Command></Exec>\n"
        L"  </Actions>\n"
        L"</Task>",
        esc_path);
    if (n < 0)
    {
        CloseHandle (h);
        return FALSE;
    }

    static const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD written = 0;
    BOOL ok = WriteFile (h, bom, 2, &written, NULL) && written == 2;
    if (ok)
    {
        DWORD nbytes = (DWORD)((size_t)n * sizeof (wchar_t));
        ok = WriteFile (h, xml, nbytes, &written, NULL) && written == nbytes;
    }
    CloseHandle (h);
    return ok;
}

static void
install_task (const wchar_t *launcher_path, const wchar_t *dir)
{
    (void)dir;

    wchar_t temp_dir[MAX_PATH];
    if (!GetTempPathW (MAX_PATH, temp_dir))
        return;

    wchar_t xml_path[MAX_PATH];
    _snwprintf (xml_path, MAX_PATH, L"%sgridflux_task.xml", temp_dir);

    if (!write_task_xml (xml_path, launcher_path))
        return;

    wchar_t sys_dir[MAX_PATH];
    if (!GetSystemDirectoryW (sys_dir, MAX_PATH))
    {
        DeleteFileW (xml_path);
        return;
    }

    wchar_t cmd[1024];
    _snwprintf (cmd, 1024, L"%s\\schtasks.exe /Create /TN \"GridFlux\" /XML \"%s\" /F",
                sys_dir, xml_path);

    STARTUPINFOW si = { .cb = sizeof (si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessW (NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi))
    {
        WaitForSingleObject (pi.hProcess, 10000);
        CloseHandle (pi.hProcess);
        CloseHandle (pi.hThread);
    }

    DeleteFileW (xml_path);
}

static void
uninstall_task (void)
{
    // Restore the Windows taskbar to its normal state.
    // When gridflux.exe is force-terminated during uninstall,
    // gf_platform_cleanup never runs, so the taskbar may be stuck
    // in auto-hide mode.  Restore it here before files are removed.
    HWND taskbar = FindWindowA ("Shell_TrayWnd", NULL);
    if (taskbar)
    {
        APPBARDATA abd = { .cbSize = sizeof (abd), .hWnd = taskbar };
        abd.lParam = ABS_ALWAYSONTOP;
        SHAppBarMessage (ABM_SETSTATE, &abd);

        ShowWindow (taskbar, SW_SHOW);
        SetWindowPos (taskbar, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);

        SendNotifyMessageA (HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    }

    // Restore secondary taskbars (multi-monitor)
    HWND secondary = NULL;
    while ((secondary = FindWindowExA (NULL, secondary, "Shell_SecondaryTrayWnd", NULL)))
    {
        ShowWindow (secondary, SW_SHOW);
    }

    // Remove the scheduled task
    wchar_t sys_dir[MAX_PATH];
    GetSystemDirectoryW (sys_dir, MAX_PATH);

    wchar_t cmd[512];
    _snwprintf (cmd, 512, L"%s\\schtasks.exe /Delete /TN \"GridFlux\" /F", sys_dir);

    STARTUPINFOW si = { .cb = sizeof (si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessW (NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi))
    {
        WaitForSingleObject (pi.hProcess, 5000);
        CloseHandle (pi.hProcess);
        CloseHandle (pi.hThread);
    }
}

// Launch gridflux.exe and restart it whenever it exits non-zero (crash). Gives
// up after repeated launch failures — most importantly when the user declines
// the UAC elevation prompt — so we never re-prompt in an endless loop.
static void
run_restart_loop (const wchar_t *exe_path, const wchar_t *dir, BOOL elevated)
{
    int launch_failures = 0;

    for (;;)
    {
        if (GetFileAttributesW (exe_path) == INVALID_FILE_ATTRIBUTES)
            break;

        HANDLE hProcess = elevated ? launch_same_level (exe_path, dir)
                                   : launch_elevated (exe_path, dir);

        if (!hProcess)
        {
            // NULL handle: ShellExecuteExW/CreateProcessW failed, typically
            // because the user declined UAC. Stop after a few attempts.
            if (++launch_failures >= 3)
            {
                MessageBoxW (NULL,
                             L"GridFlux could not start: administrator elevation "
                             L"was declined or the process failed to launch.",
                             L"GridFlux", MB_OK | MB_ICONERROR);
                break;
            }
            Sleep (RESTART_DELAY);
            continue;
        }

        launch_failures = 0;
        WaitForSingleObject (hProcess, INFINITE);

        DWORD exit_code = 0;
        GetExitCodeProcess (hProcess, &exit_code);
        CloseHandle (hProcess);

        if (exit_code == 0)
            break;

        Sleep (RESTART_DELAY);
    }
}

int WINAPI
WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    // Detect MSI commands
    const wchar_t *cmdline = GetCommandLineW ();
    if (wcsstr (cmdline, L"--install-task"))
    {
        wchar_t dir[MAX_PATH] = { 0 };
        wchar_t launcher_path[MAX_PATH] = { 0 };
        get_self_dir (dir, MAX_PATH);
        GetModuleFileNameW (NULL, launcher_path, MAX_PATH);
        install_task (launcher_path, dir);
        return 0;
    }
    if (wcsstr (cmdline, L"--uninstall-task"))
    {
        uninstall_task ();
        return 0;
    }

    // Single-instance guard
    HANDLE mutex = CreateMutexW (NULL, TRUE, MUTEX_NAME);
    if (mutex == NULL || GetLastError () == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
            CloseHandle (mutex);
        return 0;
    }

    // Build paths
    wchar_t dir[MAX_PATH] = { 0 };
    wchar_t exe_path[MAX_PATH] = { 0 };

    get_self_dir (dir, MAX_PATH);
    _snwprintf (exe_path, MAX_PATH, L"%s" EXE_NAME, dir);

    // If gridflux.exe is already running, nothing to do
    if (is_process_running (EXE_NAME))
    {
        CloseHandle (mutex);
        return 0;
    }

    BOOL elevated_at_start = is_elevated ();

    run_restart_loop (exe_path, dir, elevated_at_start);

    ReleaseMutex (mutex);
    CloseHandle (mutex);
    return 0;
}
