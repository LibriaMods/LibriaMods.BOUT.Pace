/*
 * LibriaMods BOUT Pace - speeds up the game clock with a hotkey.
 * For The Book of Unwritten Tales (32-bit, OGRE). Built as a dinput8.dll proxy: the game's input library
 * (OIS.dll) loads dinput8.dll from the game folder first; this DLL forwards DirectInput8Create to the
 * real one and patches QueryPerformanceCounter / GetTickCount in the game's modules so time runs faster.
 *
 * Keys (LibriaPace.ini next to bout.exe): F5 on/off, F6 slower, F7 faster.
 */
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
static DirectInput8Create_t realDirectInput8Create;

typedef BOOL (WINAPI *QPC_t)(LARGE_INTEGER *);
typedef DWORD (WINAPI *GTC_t)(void);
static QPC_t realQPC;
static GTC_t realGTC;

static CRITICAL_SECTION cs;
static volatile double factor = 1.0;
static LONGLONG qReal0, qFake0;
static DWORD tReal0, tFake0;

static double speed = 2.0;
static int keyToggle = VK_F5, keySlower = VK_F6, keyFaster = VK_F7;
static BOOL on = FALSE;
static char iniPath[MAX_PATH];

/* ------------------------------------------------------------------ scaled clock */

static BOOL WINAPI HookQPC(LARGE_INTEGER *c)
{
    LARGE_INTEGER r;
    BOOL ok = realQPC(&r);
    if (!ok || !c) return ok;
    EnterCriticalSection(&cs);
    c->QuadPart = qFake0 + (LONGLONG)((double)(r.QuadPart - qReal0) * factor);
    LeaveCriticalSection(&cs);
    return TRUE;
}

static DWORD WINAPI HookGTC(void)
{
    DWORD r = realGTC();
    DWORD f;
    EnterCriticalSection(&cs);
    f = tFake0 + (DWORD)((double)(DWORD)(r - tReal0) * factor);
    LeaveCriticalSection(&cs);
    return f;
}

static void SetFactor(double f)
{
    LARGE_INTEGER r;
    DWORD t;
    EnterCriticalSection(&cs);
    realQPC(&r);
    t = realGTC();
    /* re-anchor so the scaled clocks continue smoothly from where they are */
    qFake0 = qFake0 + (LONGLONG)((double)(r.QuadPart - qReal0) * factor);
    qReal0 = r.QuadPart;
    tFake0 = tFake0 + (DWORD)((double)(DWORD)(t - tReal0) * factor);
    tReal0 = t;
    factor = f;
    LeaveCriticalSection(&cs);
}

/* ------------------------------------------------------------------ IAT patching */

static void PatchModule(HMODULE mod)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY dir;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!mod || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; imp++)
    {
        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; thunk->u1.Function; thunk++)
        {
            void **slot = (void **)&thunk->u1.Function;
            void *target = NULL;
            if (*slot == (void *)realQPC) target = (void *)HookQPC;
            else if (*slot == (void *)realGTC) target = (void *)HookGTC;
            if (target)
            {
                DWORD old;
                VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
                *slot = target;
                VirtualProtect(slot, sizeof(void *), old, &old);
            }
        }
    }
}

static void PatchAll(void)
{
    const char *mods[] = { NULL, "OgreMain.dll", "OIS.dll", "ParticleUniverse.dll", "libtheoraplayer.dll",
                           "alut.dll", "OpenAL32.dll", "lua51.dll", "lua5.1.dll" };
    int i;
    PatchModule(GetModuleHandleA(NULL));
    for (i = 1; i < (int)(sizeof(mods) / sizeof(mods[0])); i++) PatchModule(GetModuleHandleA(mods[i]));
}

/* ------------------------------------------------------------------ hotkeys */

static void ShowState(void)
{
    char title[128];
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId()) return;
    if (on) sprintf(title, "The Book of Unwritten Tales  -  speed x%.1f", speed);
    else sprintf(title, "The Book of Unwritten Tales");
    SetWindowTextA(w, title);
}

static void SaveIni(void)
{
    char v[32];
    sprintf(v, "%.1f", speed);
    WritePrivateProfileStringA("Pace", "Speed", v, iniPath);
}

static BOOL Focused(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI KeyThread(LPVOID p)
{
    BOOL was[3] = { FALSE, FALSE, FALSE };
    int keys[3];
    (void)p;
    keys[0] = keyToggle; keys[1] = keySlower; keys[2] = keyFaster;
    for (;;)
    {
        int i;
        Sleep(20);
        for (i = 0; i < 3; i++)
        {
            BOOL down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
            if (down && !was[i] && Focused())
            {
                if (i == 0) on = !on;
                else
                {
                    speed += (i == 1 ? -0.5 : 0.5);
                    if (speed < 1.0) speed = 1.0;
                    if (speed > 6.0) speed = 6.0;
                    on = TRUE;
                    SaveIni();
                }
                SetFactor(on ? speed : 1.0);
                ShowState();
            }
            was[i] = down;
        }
    }
    return 0;
}

static void LoadIni(void)
{
    char v[32], *slash;
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    slash = strrchr(iniPath, '\\');
    strcpy(slash ? slash + 1 : iniPath, "LibriaPace.ini");
    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES)
    {
        WritePrivateProfileStringA("Pace", "Speed", "2.0", iniPath);
        WritePrivateProfileStringA("Pace", "ToggleKey", "116", iniPath);
        WritePrivateProfileStringA("Pace", "SlowerKey", "117", iniPath);
        WritePrivateProfileStringA("Pace", "FasterKey", "118", iniPath);
        WritePrivateProfileStringA("Pace", "Note", "Keys are Windows virtual-key codes: 116=F5 117=F6 118=F7", iniPath);
    }
    GetPrivateProfileStringA("Pace", "Speed", "2.0", v, sizeof v, iniPath);
    speed = atof(v);
    if (speed < 1.0) speed = 1.0;
    keyToggle = GetPrivateProfileIntA("Pace", "ToggleKey", VK_F5, iniPath);
    keySlower = GetPrivateProfileIntA("Pace", "SlowerKey", VK_F6, iniPath);
    keyFaster = GetPrivateProfileIntA("Pace", "FasterKey", VK_F7, iniPath);
}

/* ------------------------------------------------------------------ dinput8 proxy */

static void LoadReal(void)
{
    char path[MAX_PATH];
    HMODULE real;
    if (realDirectInput8Create) return;
    GetSystemDirectoryA(path, MAX_PATH);
    strcat(path, "\\dinput8.dll");
    real = LoadLibraryA(path);
    if (real) realDirectInput8Create = (DirectInput8Create_t)GetProcAddress(real, "DirectInput8Create");
}

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPUNKNOWN u)
{
    LoadReal();
    if (!realDirectInput8Create) return E_FAIL;
    return realDirectInput8Create(h, v, r, o, u);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        LARGE_INTEGER r;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&cs);
        realQPC = (QPC_t)GetProcAddress(k32, "QueryPerformanceCounter");
        realGTC = (GTC_t)GetProcAddress(k32, "GetTickCount");
        realQPC(&r);
        qReal0 = qFake0 = r.QuadPart;
        tReal0 = tFake0 = realGTC();
        LoadIni();
        PatchAll();
        CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
    }
    return TRUE;
}
