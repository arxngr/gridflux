// win32.c

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// clang-format off
// windows.h MUST be included before tlhelp32.h
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <wchar.h>

#define MUTEX_NAME L"GridFluxLauncherMutex"
#define EXE_NAME L"gridflux.exe"
#define RESTART_DELAY 2000 // ms to wait after a crash before restarting

// is_process_running returns TRUE if a process with the given executable name is alive
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

// get_self_dir fills 'buf' with the directory containing this .exe
static void
get_self_dir (wchar_t *buf, DWORD buf_len)
{
    GetModuleFileNameW (NULL, buf, buf_len);
    /* strip filename, keep trailing backslash */
    wchar_t *last_sep = wcsrchr (buf, L'\\');
    if (last_sep)
        *(last_sep + 1) = L'\0';
}

// WinMain
int WINAPI
WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Single-instance guard
    HANDLE mutex = CreateMutexW (NULL, TRUE, MUTEX_NAME);
    if (mutex == NULL || GetLastError () == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
            CloseHandle (mutex);
        return 0; /* Another launcher is already running */
    }

    // Build full path to gridflux.exe
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

    // Auto-restart loop
    for (;;)
    {
        // Bail out if the executable has been removed (e.g. uninstall)
        if (GetFileAttributesW (exe_path) == INVALID_FILE_ATTRIBUTES)
        {
            break;
        }

        // Build a quoted command line: "C:\...\gridflux.exe"
        wchar_t cmd[MAX_PATH + 4] = { 0 };
        _snwprintf (cmd, MAX_PATH + 4, L"\"%s\"", exe_path);

        STARTUPINFOW si = { .cb = sizeof (si) };
        PROCESS_INFORMATION pi = { 0 };

        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        BOOL created = CreateProcessW (exe_path, /* lpApplicationName  */
                                       cmd,      /* lpCommandLine       */
                                       NULL,     /* lpProcessAttributes */
                                       NULL,     /* lpThreadAttributes  */
                                       FALSE,    /* bInheritHandles     */
                                       0,        /* dwCreationFlags     */
                                       NULL,     /* lpEnvironment       */
                                       dir,      /* lpCurrentDirectory  */
                                       &si, &pi);

        if (!created)
        {
            // Could not launch — wait and retry
            Sleep (RESTART_DELAY);
            continue;
        }

        // Wait until gridflux.exe terminates
        WaitForSingleObject (pi.hProcess, INFINITE);

        DWORD exit_code = 0;
        GetExitCodeProcess (pi.hProcess, &exit_code);

        CloseHandle (pi.hProcess);
        CloseHandle (pi.hThread);

        // Clean shutdown → do not restart
        if (exit_code == 0)
            break;

        // Crash or forced kill → wait, then restart
        Sleep (RESTART_DELAY);
    }

    ReleaseMutex (mutex);
    CloseHandle (mutex);
    return 0;
}
