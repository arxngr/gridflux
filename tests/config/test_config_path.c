#include "config/config.h"
#include "platform/windows/taskbar.h"
#include <assert.h>
#include <direct.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wchar_t received_args[4096 + MAX_PATH + 64];

static BOOL WINAPI
capture_process (LPCWSTR app, LPWSTR command, LPSECURITY_ATTRIBUTES process_security,
                 LPSECURITY_ATTRIBUTES thread_security, BOOL inherit, DWORD flags,
                 LPVOID environment, LPCWSTR directory, LPSTARTUPINFOW startup,
                 LPPROCESS_INFORMATION process)
{
    (void)app;
    (void)process_security;
    (void)thread_security;
    (void)inherit;
    (void)environment;
    (void)directory;
    assert ((flags & CREATE_NO_WINDOW) && startup->wShowWindow == SW_HIDE);
    wcscpy (received_args, command);
    process->hProcess = (HANDLE)(UINT_PTR)1;
    process->hThread = NULL;
    return TRUE;
}

static BOOL WINAPI
capture_elevation (SHELLEXECUTEINFOW *info)
{
    assert (wcscmp (info->lpVerb, L"runas") == 0 && info->nShow == SW_HIDE);
    wcscpy (received_args, info->lpParameters);
    info->hProcess = (HANDLE)(UINT_PTR)1;
    return TRUE;
}

void
gf_taskbar_restore_all (void)
{
}

// Inspect the production launcher calls without starting a process or UAC.
#define CreateProcessW capture_process
#define ShellExecuteExW capture_elevation
#define WinMain test_launcher_entry
#include "../../src/launcher/win32.c"
#undef CreateProcessW
#undef ShellExecuteExW
#undef WinMain

int
main (void)
{
    char directory[4096], caller_profile[4096], admin_profile[4096];
    assert (GetCurrentDirectoryA (sizeof (directory), directory));
    assert (snprintf (caller_profile, sizeof (caller_profile), "%s\\caller profile",
                      directory)
            > 0);
    assert (
        snprintf (admin_profile, sizeof (admin_profile), "%s\\admin profile", directory)
        > 0);
    assert (_mkdir (caller_profile) == 0);
    assert (_mkdir (admin_profile) == 0);
    assert (_putenv_s ("APPDATA", caller_profile) == 0);
    char caller_path[4096];
    strcpy (caller_path, gf_config_get_path ());
    wchar_t args[4096 + 32], command[4096 + MAX_PATH + 64];
    assert (gf_config_get_launch_args (args, sizeof (args) / sizeof (args[0])));
    assert (!gf_config_get_launch_args (command, 4));
    assert (!gf_config_get_launch_args (NULL, 4));

    const wchar_t *exe = L"C:\\Program Files\\GridFlux\\gridflux.exe";
    assert (launch_same_level (exe, L"C:\\Program Files\\GridFlux", args));
    wcscpy (command, received_args);
    int count;
    wchar_t **argv = CommandLineToArgvW (command, &count);
    assert (argv && count == 3 && wcscmp (argv[0], exe) == 0
            && wcscmp (argv[1], L"--config") == 0);
    char selected[4096];
    assert (WideCharToMultiByte (CP_ACP, 0, argv[2], -1, selected, sizeof (selected),
                                 NULL, NULL));
    assert (strcmp (selected, caller_path) == 0);
    LocalFree (argv);
    assert (launch_elevated (exe, L"C:\\Program Files\\GridFlux", args));
    assert (wcscmp (received_args, args) == 0);

    // Reproduce credential elevation: the server has a different APPDATA.
    assert (_putenv_s ("APPDATA", admin_profile) == 0);
    assert (strcmp (gf_config_get_path (), caller_path) != 0);
    assert (gf_config_set_path (selected) == GF_SUCCESS);
    assert (strcmp (gf_config_get_path (), caller_path) == 0);
    assert (gf_config_set_path (NULL) == GF_ERROR_INVALID_PARAMETER);
    assert (gf_config_set_path ("") == GF_ERROR_INVALID_PARAMETER);
    assert (strcmp (gf_config_get_path (), caller_path) == 0);
    char excessive[4097];
    memset (excessive, 'x', sizeof (excessive) - 1);
    excessive[sizeof (excessive) - 1] = '\0';
    assert (gf_config_set_path (excessive) == GF_ERROR_INVALID_PARAMETER);
    assert (gf_config_set_path (gf_config_get_path ()) == GF_SUCCESS);

    char child[4096];
    snprintf (child, sizeof (child), "%s\\gridflux", caller_profile);
    assert (_rmdir (child) == 0);
    snprintf (child, sizeof (child), "%s\\gridflux", admin_profile);
    assert (_rmdir (child) == 0);
    assert (_rmdir (caller_profile) == 0);
    assert (_rmdir (admin_profile) == 0);
    puts ("Configuration identity survives elevation under another account");
    return 0;
}
