// ==WindhawkMod==
// @id              kdeconnectd-restarter
// @name            kdeconnectd Crash Restarter
// @description     Attempts to restart kdeconnectd upon crashing
// @version         0.1
// @author          qwerty12
// @github          https://github.com/qwerty12
// @include         kdeconnect-indicator.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# kdeconnectd Crash Restarter
[KDE Connect](https://kdeconnect.kde.org/) is an extremely useful program, and
I thank the KDE community for graciously making it available on Windows.

After a while, however, kdeconnectd does crash here. Here's a crude hack to restart it.

This could be implemented as an external program, but I prefer to write it as a
[Windhawk](https://windhawk.net/) mod for two reasons:

* it'll die when the indicator does (e.g. when the Store is updating it)

    * I also don't want to `LoadLibrary` KDE Connect's dbus-1-3.dll externally because of the above

* I don't have to use [hacks](https://gist.github.com/qwerty12/de1c6bad9bc9db9530ee2ed2791f297b) to [determine](https://gist.github.com/qwerty12/6839e39b2b41072d22f564a366f7eacc) KDE Connect's path and [connect to the bus](https://github.com/pdf/kdeconnect-chrome-extension/pull/44)

Thanks to m417z for Windhawk.
*/
// ==/WindhawkModReadme==

// NOTE: I intentionally let the OS do the cleaning up

#include <handleapi.h>
#include <psapi.h>
#include <winnt.h>

#define DBUS_MODULE_NAME L"dbus-1-3.dll"
#define KDECONNECT_SERVICE "org.kde.kdeconnect"

typedef INT (*start)(VOID);
static start Start = nullptr;

#define DBUS_TYPE_INVALID ((INT) '\0')
#define DBUS_TYPE_STRING  ((INT) 's')
#define DBUS_TYPE_UINT32  ((INT) 'u')
struct DBusConnection;
struct DBusMessage;
static VOID            (CDECL *dbus_bus_add_match)(DBusConnection*, LPCSTR, LPVOID) = nullptr;
static DBusConnection* (CDECL *dbus_bus_get_private)(INT, LPVOID) = nullptr;
static VOID            (CDECL *dbus_connection_flush)(DBusConnection*) = nullptr;
static DBusMessage*    (CDECL *dbus_connection_pop_message)(DBusConnection*) = nullptr;
static DWORD           (CDECL *dbus_connection_read_write)(DBusConnection*, INT) = nullptr;
static DWORD           (CDECL *dbus_connection_send)(DBusConnection*, DBusMessage*, LPVOID) = nullptr;
static VOID            (CDECL *dbus_connection_set_exit_on_disconnect)(DBusConnection*, DWORD) = nullptr;
static VOID            (CDECL *dbus_connection_unref)(DBusConnection*) = nullptr;
static DWORD           (CDECL *dbus_message_append_args)(DBusMessage*, INT, ...) = nullptr;
static DWORD           (CDECL *dbus_message_get_args)(DBusMessage*, LPVOID, INT, ...) = nullptr;
static DWORD           (CDECL *dbus_message_is_signal)(DBusMessage*, LPCSTR, LPCSTR) = nullptr;
static DBusMessage*    (CDECL *dbus_message_new_method_call)(LPCSTR, LPCSTR, LPCSTR, LPCSTR) = nullptr;
static VOID            (CDECL *dbus_message_set_no_reply)(DBusMessage*, DWORD) = nullptr;
static VOID            (CDECL *dbus_message_unref)(DBusMessage*) = nullptr;

static VOID StartKdeConnect(DBusConnection *conn)
{
    DBusMessage *msg = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "StartServiceByName");
    if (!msg)
        return;

	if (dbus_message_append_args(msg, DBUS_TYPE_STRING, (LPCSTR[]) { KDECONNECT_SERVICE }, DBUS_TYPE_UINT32, (DWORD[]) {0}, DBUS_TYPE_INVALID)) {
        dbus_message_set_no_reply(msg, TRUE);
        dbus_connection_send(conn, msg, nullptr);
        dbus_connection_flush(conn);
    }

    dbus_message_unref(msg);
}

static DWORD WINAPI ThreadProc(LPVOID lpParameter)
{
    DBusConnection *conn = (DBusConnection*)lpParameter;

    dbus_bus_add_match(conn, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='" KDECONNECT_SERVICE "'", nullptr);
    dbus_connection_flush(conn);

    while (dbus_connection_read_write(conn, -1)) {
        DBusMessage *msg = dbus_connection_pop_message(conn);
        if (!msg) {
            Sleep(30000);
            continue;
        }

        if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
            LPCSTR name;
            LPCSTR old_owner;
            LPCSTR new_owner;

            if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID))
                goto msg_unref;

            if (!name || !*name)
                goto msg_unref;

            Wh_Log(L"%S %S %S\n", name, old_owner, new_owner);

            if (((!strcmp(name, KDECONNECT_SERVICE))) && ((!new_owner) || (new_owner && !*new_owner)))
                StartKdeConnect(conn);
        }

        msg_unref:
            dbus_message_unref(msg);
    }

    dbus_connection_unref(conn);
    return 0;
}

INT DetouredStart()
{
    HMODULE hmodDbus = LoadLibraryExW(DBUS_MODULE_NAME, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    if (hmodDbus) {
        start dbus_threads_init_default;
        *(FARPROC*)&dbus_threads_init_default = GetProcAddress(hmodDbus, "dbus_threads_init_default");
        if (dbus_threads_init_default && dbus_threads_init_default()) {
            DBusConnection *conn;
            *(FARPROC*)&dbus_bus_add_match = GetProcAddress(hmodDbus, "dbus_bus_add_match");
            *(FARPROC*)&dbus_bus_get_private = GetProcAddress(hmodDbus, "dbus_bus_get_private");
            *(FARPROC*)&dbus_connection_flush = GetProcAddress(hmodDbus, "dbus_connection_flush");
            *(FARPROC*)&dbus_connection_pop_message = GetProcAddress(hmodDbus, "dbus_connection_pop_message");
            *(FARPROC*)&dbus_connection_read_write = GetProcAddress(hmodDbus, "dbus_connection_read_write");
            *(FARPROC*)&dbus_connection_send = GetProcAddress(hmodDbus, "dbus_connection_send");
            *(FARPROC*)&dbus_connection_set_exit_on_disconnect = GetProcAddress(hmodDbus, "dbus_connection_set_exit_on_disconnect");
            *(FARPROC*)&dbus_connection_unref = GetProcAddress(hmodDbus, "dbus_connection_unref");
            *(FARPROC*)&dbus_message_append_args = GetProcAddress(hmodDbus, "dbus_message_append_args");
            *(FARPROC*)&dbus_message_get_args = GetProcAddress(hmodDbus, "dbus_message_get_args");
            *(FARPROC*)&dbus_message_is_signal = GetProcAddress(hmodDbus, "dbus_message_is_signal");
            *(FARPROC*)&dbus_message_new_method_call = GetProcAddress(hmodDbus, "dbus_message_new_method_call");
            *(FARPROC*)&dbus_message_set_no_reply = GetProcAddress(hmodDbus, "dbus_message_set_no_reply");
            *(FARPROC*)&dbus_message_unref = GetProcAddress(hmodDbus, "dbus_message_unref");

            if ((conn = dbus_bus_get_private(0, nullptr))) {
                HANDLE hThread;
                dbus_connection_set_exit_on_disconnect(conn, FALSE);
                if ((hThread = CreateThread(nullptr, 0, ThreadProc, conn, 0, nullptr))) {
                    SetThreadPriority(hThread, THREAD_PRIORITY_LOWEST);
                    CloseHandle(hThread);
                }
            } else {
                Wh_Log(L"Couldn't connect to KDE Connect's Session bus");
            }
        } else {
            Wh_Log(L"Couldn't find symbol dbus_threads_init_default()");
        }
    } else {
        Wh_Log(L"Couldn't load " DBUS_MODULE_NAME);
    }

    return Start();
}

BOOL Wh_ModInit() {
    MODULEINFO mi;

	if (GetModuleInformation(GetCurrentProcess(), GetModuleHandle(nullptr), &mi, sizeof(mi))) {
		LPVOID entry = mi.EntryPoint;

		if (entry) {
            Wh_Log(L"Init " WH_MOD_ID L" version " WH_MOD_VERSION);
            Wh_SetFunctionHook((void*)entry, (void*)DetouredStart, (void**)&Start);
            return TRUE;
        }
	}

    Wh_Log(L"Failed to find entrypoint");
    return FALSE;
}
