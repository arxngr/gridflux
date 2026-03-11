/*  win32.c — GridFlux Launcher */
/*  */
/*  Runs as asInvoker (no elevation). */
/*  Supports --install-task to silently create a Task Scheduler entry so */
/*  GridFlux auto-starts at logon elevated without a UAC prompt. */
/*  Auto-restarts gridflux.exe on crash; stops on clean shutdown (exit 0). */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

/*  clang-format off */
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
/*  clang-format on */

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

static void
install_task (const wchar_t *launcher_path, const wchar_t *dir)
{
    wchar_t temp_dir[MAX_PATH];
    GetTempPathW (MAX_PATH, temp_dir);

    wchar_t xml_path[MAX_PATH];
    _snwprintf (xml_path, MAX_PATH, L"%sgridflux_task.xml", temp_dir);

    FILE *f = _wfopen (xml_path, L"wb");
    if (!f)
        return;

    /*  Write UTF-16 LE BOM */
    fputc (0xFF, f);
    fputc (0xFE, f);

    wchar_t xml[2048];
    _snwprintf (xml, 2048,
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
                launcher_path);

    fwrite (xml, sizeof (wchar_t), wcslen (xml), f);
    fclose (f);

    wchar_t sys_dir[MAX_PATH];
    GetSystemDirectoryW (sys_dir, MAX_PATH);

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

int WINAPI
WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    /*  Detect MSI commands */
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

    /*  Single-instance guard */
    HANDLE mutex = CreateMutexW (NULL, TRUE, MUTEX_NAME);
    if (mutex == NULL || GetLastError () == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
            CloseHandle (mutex);
        return 0;
    }

    /*  Build paths */
    wchar_t dir[MAX_PATH] = { 0 };
    wchar_t exe_path[MAX_PATH] = { 0 };

    get_self_dir (dir, MAX_PATH);
    _snwprintf (exe_path, MAX_PATH, L"%s" EXE_NAME, dir);

    /*  If gridflux.exe is already running, nothing to do */
    if (is_process_running (EXE_NAME))
    {
        CloseHandle (mutex);
        return 0;
    }

    BOOL elevated_at_start = is_elevated ();

    for (;;)
    {
        if (GetFileAttributesW (exe_path) == INVALID_FILE_ATTRIBUTES)
            break;

        HANDLE hProcess;

        if (elevated_at_start)
            hProcess = launch_same_level (exe_path, dir);
        else
            hProcess = launch_elevated (exe_path, dir);

        if (!hProcess)
        {
            Sleep (RESTART_DELAY);
            continue;
        }

        WaitForSingleObject (hProcess, INFINITE);

        DWORD exit_code = 0;
        GetExitCodeProcess (hProcess, &exit_code);
        CloseHandle (hProcess);

        if (exit_code == 0)
            break;

        Sleep (RESTART_DELAY);
    }

    ReleaseMutex (mutex);
    CloseHandle (mutex);
    return 0;
}
