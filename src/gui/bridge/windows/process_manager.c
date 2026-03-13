#include "../process_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdio.h>
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
// clang-format on

#define GF_SERVER_EXE_W L"gridflux.exe"

static DWORD
find_server_pid (void)
{
    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe = { .dwSize = sizeof (pe) };
    DWORD pid = 0;

    if (Process32FirstW (snap, &pe))
    {
        do
        {
            if (_wcsicmp (pe.szExeFile, GF_SERVER_EXE_W) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW (snap, &pe));
    }

    CloseHandle (snap);
    return pid;
}

static void
get_exe_dir (wchar_t *buf, DWORD buf_len)
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

bool
gf_server_is_running (void)
{
    return find_server_pid () != 0;
}

bool
gf_server_start (void)
{
    if (gf_server_is_running ())
        return true; // already running

    wchar_t dir[MAX_PATH] = { 0 };
    wchar_t exe_path[MAX_PATH] = { 0 };

    get_exe_dir (dir, MAX_PATH);
    _snwprintf (exe_path, MAX_PATH, L"%s" GF_SERVER_EXE_W, dir);

    if (GetFileAttributesW (exe_path) == INVALID_FILE_ATTRIBUTES)
        return false; // executable not found

    // gridflux.exe has requireAdministrator in its manifest.
    // If we are already elevated, use CreateProcessW (no UAC prompt).
    // Otherwise, use ShellExecuteExW with "runas" to request elevation.
    if (is_elevated ())
    {
        wchar_t cmd[MAX_PATH + 4] = { 0 };
        _snwprintf (cmd, MAX_PATH + 4, L"\"%s\"", exe_path);

        STARTUPINFOW si = { .cb = sizeof (si) };
        PROCESS_INFORMATION pi = { 0 };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        BOOL ok = CreateProcessW (exe_path, cmd, NULL, NULL, FALSE,
                                  CREATE_NO_WINDOW, NULL, dir, &si, &pi);
        if (!ok)
            return false;

        CloseHandle (pi.hThread);
        CloseHandle (pi.hProcess);
    }
    else
    {
        SHELLEXECUTEINFOW sei = { 0 };
        sei.cbSize = sizeof (sei);
        sei.fMask = SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";
        sei.lpFile = exe_path;
        sei.lpDirectory = dir;
        sei.nShow = SW_HIDE;

        if (!ShellExecuteExW (&sei))
            return false;
    }

    return true;
}

bool
gf_server_stop (void)
{
    DWORD pid = find_server_pid ();
    if (pid == 0)
        return false; // not running

    HANDLE proc = OpenProcess (PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!proc)
        return false;

    BOOL ok = TerminateProcess (proc, 1);
    if (ok)
    {
        // wait for the process to actually exit (up to 5 seconds)
        WaitForSingleObject (proc, 5000);
    }

    CloseHandle (proc);
    return ok != 0;
}
