/*
 * gui_plugin_bridge.cpp — Plugin GUI: native platform window embedded in DAW
 *
 * Creates a native child window (Win32 HWND / X11 Window), sets up an OpenGL
 * context, and drives ImGui rendering on a dedicated thread using the shared
 * fx_gui_render_frame() from gui_render.h.
 *
 * This replaces the SDL2-based implementation to eliminate the SDL2 dependency
 * for plugins, solving multi-plugin coexistence issues in DAWs.
 *
 * Platform backends:
 * - Windows: Win32 API + wglCreateContext + ImGui_ImplWin32
 * - Linux:   X11 + glXCreateContext + manual event mapping
 * - macOS:   stub (future: NSOpenGLView + Objective-C)
 */

#include "imgui.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "../gui/gui_render.h"
#include "../gui/texture.h"
#include "../engine/fx_engine.h"
}

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdarg>

/* ═══════════════════════════════════════════════════════════════════════════
 * Windows implementation — Win32 + WGL + ImGui_ImplWin32
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifdef _WIN32

#include "imgui_impl_win32.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

/* Forward declare ImGui Win32 WndProc handler */
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* Custom window class name */
static const wchar_t *OXFX_WND_CLASS = L"0xFX_PluginGUI";
static bool s_wnd_class_registered = false;

/* WGL function typedefs for creating modern GL context */
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int *);
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_FLAGS_ARB         0x2094

/* Pixel format attribute constants for wglChoosePixelFormatARB */
typedef BOOL (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
#define WGL_DRAW_TO_WINDOW_ARB        0x2001
#define WGL_SUPPORT_OPENGL_ARB        0x2010
#define WGL_DOUBLE_BUFFER_ARB         0x2011
#define WGL_PIXEL_TYPE_ARB            0x2013
#define WGL_TYPE_RGBA_ARB             0x202B
#define WGL_COLOR_BITS_ARB            0x2014
#define WGL_DEPTH_BITS_ARB            0x2022
#define WGL_STENCIL_BITS_ARB          0x2023
#define WGL_ACCELERATION_ARB          0x2003
#define WGL_FULL_ACCELERATION_ARB     0x2027

struct PluginGUI {
    fx_engine_t    *engine;
    fx_gui_state_t *gui_state;
    HWND            hwnd;
    HDC             hdc;
    HGLRC           hglrc;
    HANDLE          render_thread;
    volatile bool   running;
    volatile bool   visible;
    uint32_t        width;
    uint32_t        height;
    void           *parent_handle;
    ImGuiContext   *imgui_ctx;
};

/* ─── WndProc ──────────────────────────────────────────────────────────── */

static LRESULT CALLBACK PluginWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    /* Get our PluginGUI pointer from the window userdata.
     * During CreateWindowExW, GWLP_USERDATA isn't set yet — gui is NULL.
     * The ImGui context is created on the render thread — imgui_ctx may be NULL.
     * We MUST NOT call ImGui functions without a valid context. */
    PluginGUI *gui = (PluginGUI *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (gui && gui->imgui_ctx) {
        ImGui::SetCurrentContext(gui->imgui_ctx);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
            return 0;
    }
    /* No valid gui/context — skip ImGui, just handle basic messages */

    switch (msg) {
    case WM_CHAR:
        return 0; /* consume — prevent Windows error beep */
    case WM_SYSCHAR:
        return 0; /* consume Alt+key beeps */
    case WM_ERASEBKGND:
        return 1; /* we handle painting via OpenGL */
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ─── WGL helpers ──────────────────────────────────────────────────────── */

static bool setup_pixel_format(HDC hdc)
{
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf) return false;
    return SetPixelFormat(hdc, pf, &pfd) != 0;
}

static HGLRC create_gl33_context(HDC hdc)
{
    /* First create a legacy context to load wglCreateContextAttribsARB */
    HGLRC legacy = wglCreateContext(hdc);
    if (!legacy) return NULL;
    wglMakeCurrent(hdc, legacy);

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    if (!wglCreateContextAttribsARB) {
        /* No ARB extension — fall back to legacy context */
        return legacy;
    }

    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    HGLRC modern = wglCreateContextAttribsARB(hdc, NULL, attribs);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);

    return modern;
}

/* ─── Render Thread ────────────────────────────────────────────────────── */

/* Debug log — same as plugin.c but we can't see that one from here */
#ifdef _WIN32
static void gui_dbg(const char *fmt, ...) {
    static CRITICAL_SECTION cs; static bool init = false;
    if (!init) { InitializeCriticalSection(&cs); init = true; }
    EnterCriticalSection(&cs);
    FILE *f = fopen("C:\\Users\\Dan Michael\\Desktop\\0xfx_plugin_debug.log", "a");
    if (f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] [TID %5lu] [GUI] ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                (unsigned long)GetCurrentThreadId());
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fprintf(f, "\n"); fflush(f); fclose(f);
    }
    LeaveCriticalSection(&cs);
}
#else
static void gui_dbg(const char *fmt, ...) { (void)fmt; }
#endif

/* Global mutex for GL initialization — WGL is not thread-safe */
static CRITICAL_SECTION s_gl_init_cs;
static bool s_gl_init_cs_ready = false;

static DWORD WINAPI render_thread_func(LPVOID data)
{
    PluginGUI *gui = (PluginGUI *)data;
    gui_dbg("render_thread START engine=%p", gui->engine);

    /* Serialize GL/ImGui initialization — WGL can't handle concurrent
     * context operations across threads on Windows */
    if (!s_gl_init_cs_ready) {
        InitializeCriticalSection(&s_gl_init_cs);
        s_gl_init_cs_ready = true;
    }
    EnterCriticalSection(&s_gl_init_cs);
    gui_dbg("render_thread GL init lock acquired");

    wglMakeCurrent(gui->hdc, gui->hglrc);

    /* ImGui setup on render thread */
    IMGUI_CHECKVERSION();
    gui->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui->imgui_ctx);

    /* Disable imgui.ini — DAW working dirs are unpredictable */
    ImGui::GetIO().IniFilename = NULL;
    ImGui::GetIO().ConfigDebugHighlightIdConflicts = false;

    fx_gui_setup_theme();

    ImGui_ImplWin32_InitForOpenGL(gui->hwnd);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* Create the GUI rendering state (bound to the engine) */
    gui->gui_state = fx_gui_create(gui->engine);

    gui_dbg("render_thread GL init complete, releasing lock");
    LeaveCriticalSection(&s_gl_init_cs);

    while (gui->running) {
        if (!gui->visible) {
            Sleep(50);
            continue;
        }

        /* Process pending Win32 messages for our window */
        MSG msg;
        while (PeekMessageW(&msg, gui->hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        /* Track host window resize */
        {
            HWND parent = GetParent(gui->hwnd);
            if (parent) {
                RECT rc;
                GetClientRect(parent, &rc);
                int pw = rc.right - rc.left;
                int ph = rc.bottom - rc.top;
                if (pw > 0 && ph > 0 &&
                    ((uint32_t)pw != gui->width || (uint32_t)ph != gui->height)) {
                    gui->width = (uint32_t)pw;
                    gui->height = (uint32_t)ph;
                    SetWindowPos(gui->hwnd, NULL, 0, 0, pw, ph,
                                 SWP_NOZORDER | SWP_NOMOVE);
                }
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        /* Render the full 0xFX GUI via the shared renderer */
        fx_gui_render_frame(gui->gui_state, (float)gui->width, (float)gui->height,
                            true /* is_plugin */);

        ImGui::Render();

        glViewport(0, 0, (int)gui->width, (int)gui->height);
        glClearColor(0.06f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SwapBuffers(gui->hdc);

        Sleep(16); /* ~60fps */
    }

    /* Cleanup on render thread */
    if (gui->gui_state) {
        fx_gui_destroy(gui->gui_state);
        gui->gui_state = NULL;
    }

    /* Flush texture cache — GL IDs are invalid after context destruction */
    /* NOTE: dont call fx_texture_shutdown — global cache shared across instances */

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(gui->imgui_ctx);
    gui->imgui_ctx = NULL;

    wglMakeCurrent(NULL, NULL);

    return 0;
}

/* ─── Window class registration ────────────────────────────────────────── */

static void ensure_wnd_class(void)
{
    if (s_wnd_class_registered) return;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = PluginWndProc;
    wc.hInstance      = GetModuleHandleW(NULL);
    wc.lpszClassName  = OXFX_WND_CLASS;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);

    if (RegisterClassExW(&wc))
        s_wnd_class_registered = true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Linux implementation — X11 + GLX (stub with basic structure)
 * ═══════════════════════════════════════════════════════════════════════════ */
#elif defined(__linux__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <pthread.h>
#include <unistd.h>

struct PluginGUI {
    fx_engine_t    *engine;
    fx_gui_state_t *gui_state;
    Display        *display;
    Window          window;
    GLXContext      gl_ctx;
    Colormap        colormap;
    pthread_t       render_thread;
    volatile bool   running;
    volatile bool   visible;
    uint32_t        width;
    uint32_t        height;
    void           *parent_handle;
    ImGuiContext   *imgui_ctx;
    bool            thread_created;
};

/* Map X11 mouse button to ImGui mouse button index */
static int x11_button_to_imgui(int button)
{
    switch (button) {
    case 1: return 0; /* Left */
    case 2: return 2; /* Middle */
    case 3: return 1; /* Right */
    default: return -1;
    }
}

/* Map X11 KeySym to ImGuiKey */
static ImGuiKey x11_keysym_to_imgui(KeySym ks)
{
    if (ks >= 'a' && ks <= 'z') return (ImGuiKey)(ImGuiKey_A + (ks - 'a'));
    if (ks >= 'A' && ks <= 'Z') return (ImGuiKey)(ImGuiKey_A + (ks - 'A'));
    if (ks >= '0' && ks <= '9') return (ImGuiKey)(ImGuiKey_0 + (ks - '0'));

    switch (ks) {
    case XK_Tab:       return ImGuiKey_Tab;
    case XK_Left:      return ImGuiKey_LeftArrow;
    case XK_Right:     return ImGuiKey_RightArrow;
    case XK_Up:        return ImGuiKey_UpArrow;
    case XK_Down:      return ImGuiKey_DownArrow;
    case XK_Home:      return ImGuiKey_Home;
    case XK_End:       return ImGuiKey_End;
    case XK_Delete:    return ImGuiKey_Delete;
    case XK_BackSpace: return ImGuiKey_Backspace;
    case XK_Return:    return ImGuiKey_Enter;
    case XK_Escape:    return ImGuiKey_Escape;
    case XK_space:     return ImGuiKey_Space;
    default:           return ImGuiKey_None;
    }
}

static void process_x11_events(PluginGUI *gui)
{
    ImGuiIO &io = ImGui::GetIO();

    while (XPending(gui->display)) {
        XEvent ev;
        XNextEvent(gui->display, &ev);

        switch (ev.type) {
        case MotionNotify:
            io.AddMousePosEvent((float)ev.xmotion.x, (float)ev.xmotion.y);
            break;

        case ButtonPress: {
            int btn = x11_button_to_imgui(ev.xbutton.button);
            if (btn >= 0) io.AddMouseButtonEvent(btn, true);
            /* Scroll wheel */
            if (ev.xbutton.button == 4) io.AddMouseWheelEvent(0, 1.0f);
            if (ev.xbutton.button == 5) io.AddMouseWheelEvent(0, -1.0f);
            break;
        }

        case ButtonRelease: {
            int btn = x11_button_to_imgui(ev.xbutton.button);
            if (btn >= 0) io.AddMouseButtonEvent(btn, false);
            break;
        }

        case KeyPress:
        case KeyRelease: {
            bool down = (ev.type == KeyPress);
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            ImGuiKey key = x11_keysym_to_imgui(ks);
            if (key != ImGuiKey_None)
                io.AddKeyEvent(key, down);

            /* Text input on key press */
            if (down) {
                char buf[8] = {};
                int len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, NULL, NULL);
                for (int i = 0; i < len; i++) {
                    if ((unsigned char)buf[i] >= 32)
                        io.AddInputCharacter((unsigned int)(unsigned char)buf[i]);
                }
            }
            break;
        }

        case ConfigureNotify:
            if ((uint32_t)ev.xconfigure.width != gui->width ||
                (uint32_t)ev.xconfigure.height != gui->height) {
                gui->width = (uint32_t)ev.xconfigure.width;
                gui->height = (uint32_t)ev.xconfigure.height;
            }
            break;

        case FocusIn:
            io.AddFocusEvent(true);
            break;
        case FocusOut:
            io.AddFocusEvent(false);
            break;

        default:
            break;
        }
    }
}

static void *render_thread_func(void *data)
{
    PluginGUI *gui = (PluginGUI *)data;

    glXMakeCurrent(gui->display, gui->window, gui->gl_ctx);

    /* ImGui setup on render thread */
    IMGUI_CHECKVERSION();
    gui->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui->imgui_ctx);

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigDebugHighlightIdConflicts = false;
    io.DisplaySize = ImVec2((float)gui->width, (float)gui->height);

    /* Apply the shared 0xFX "worn grime" theme */
    fx_gui_setup_theme();

    /* No ImGui_ImplX11 backend — we feed events manually */
    ImGui_ImplOpenGL3_Init("#version 330");

    gui->gui_state = fx_gui_create(gui->engine);

    while (gui->running) {
        if (!gui->visible) {
            usleep(50000);
            continue;
        }

        process_x11_events(gui);

        /* Update display size */
        io.DisplaySize = ImVec2((float)gui->width, (float)gui->height);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        fx_gui_render_frame(gui->gui_state, (float)gui->width, (float)gui->height,
                            true);

        ImGui::Render();

        glViewport(0, 0, (int)gui->width, (int)gui->height);
        glClearColor(0.06f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glXSwapBuffers(gui->display, gui->window);

        usleep(16000); /* ~60fps */
    }

    if (gui->gui_state) {
        fx_gui_destroy(gui->gui_state);
        gui->gui_state = NULL;
    }

    /* NOTE: dont call fx_texture_shutdown — global cache shared across instances */

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(gui->imgui_ctx);
    gui->imgui_ctx = NULL;

    glXMakeCurrent(gui->display, None, NULL);

    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * macOS stub
 * ═══════════════════════════════════════════════════════════════════════════ */
#elif defined(__APPLE__)

struct PluginGUI {
    fx_engine_t    *engine;
    fx_gui_state_t *gui_state;
    volatile bool   running;
    volatile bool   visible;
    uint32_t        width;
    uint32_t        height;
    void           *parent_handle;
    ImGuiContext   *imgui_ctx;
};

#endif /* platform */

/* ═══════════════════════════════════════════════════════════════════════════
 * Public C API — platform-dispatched
 * ═══════════════════════════════════════════════════════════════════════════ */

extern "C" {

void oxfx_gui_detach(void *gui_ptr);

void *oxfx_gui_create(void *engine)
{
    gui_dbg("gui_create engine=%p", engine);
    PluginGUI *gui = new PluginGUI();
    memset(gui, 0, sizeof(*gui));
    gui->engine = (fx_engine_t *)engine;
    gui->width  = 1200;
    gui->height = 700;
    gui_dbg("gui_create done gui=%p", gui);
    return gui;
}

void oxfx_gui_destroy(void *gui_ptr)
{
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    oxfx_gui_detach(gui_ptr);
    delete gui;
}

/* ─── Attach (create window + start render thread) ─────────────────────── */
/* Multi-instance safe: all mutable GUI state lives in the per-instance
 * fx_gui_state_t struct. No single-instance guard needed. */

void oxfx_gui_attach(void *gui_ptr, void *parent_hwnd)
{
    gui_dbg("gui_attach gui=%p parent=%p", gui_ptr, parent_hwnd);
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    if (gui->running) return;

    gui->parent_handle = parent_hwnd;

#ifdef _WIN32
    /* Register our window class (once) */
    ensure_wnd_class();

    /* Create child window inside the host's parent HWND */
    gui->hwnd = CreateWindowExW(
        0,
        OXFX_WND_CLASS,
        L"0xFX",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, (int)gui->width, (int)gui->height,
        (HWND)parent_hwnd,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );
    if (!gui->hwnd) {
        fprintf(stderr, "0xFX Plugin GUI: CreateWindowExW failed (error %lu)\n",
                GetLastError());
        return;
    }

    /* Store our PluginGUI pointer on the window for WndProc to find */
    SetWindowLongPtrW(gui->hwnd, GWLP_USERDATA, (LONG_PTR)gui);

    gui->hdc = GetDC(gui->hwnd);
    if (!gui->hdc) {
        fprintf(stderr, "0xFX Plugin GUI: GetDC failed\n");
        DestroyWindow(gui->hwnd);
        gui->hwnd = NULL;
        return;
    }

    if (!setup_pixel_format(gui->hdc)) {
        fprintf(stderr, "0xFX Plugin GUI: pixel format setup failed\n");
        ReleaseDC(gui->hwnd, gui->hdc);
        DestroyWindow(gui->hwnd);
        gui->hwnd = NULL;
        gui->hdc = NULL;
        return;
    }

    gui->hglrc = create_gl33_context(gui->hdc);
    if (!gui->hglrc) {
        fprintf(stderr, "0xFX Plugin GUI: GL context creation failed\n");
        ReleaseDC(gui->hwnd, gui->hdc);
        DestroyWindow(gui->hwnd);
        gui->hwnd = NULL;
        gui->hdc = NULL;
        return;
    }

    /* Release GL context from this thread before starting render thread */
    wglMakeCurrent(NULL, NULL);

    gui->running = true;
    gui->visible = true;
    gui->render_thread = CreateThread(NULL, 0, render_thread_func, gui, 0, NULL);

#elif defined(__linux__)
    /* Open a separate X display connection for this plugin instance.
     * This avoids thread-safety issues with the host's Display*. */
    gui->display = XOpenDisplay(NULL);
    if (!gui->display) {
        fprintf(stderr, "0xFX Plugin GUI: XOpenDisplay failed\n");
        return;
    }

    int screen = DefaultScreen(gui->display);

    /* Choose a visual with GLX */
    int attribs[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        None
    };
    XVisualInfo *vi = glXChooseVisual(gui->display, screen, attribs);
    if (!vi) {
        fprintf(stderr, "0xFX Plugin GUI: glXChooseVisual failed\n");
        XCloseDisplay(gui->display);
        gui->display = NULL;
        return;
    }

    /* Create colormap + window */
    Window parent_win = parent_hwnd ? (Window)(uintptr_t)parent_hwnd
                                    : RootWindow(gui->display, screen);
    gui->colormap = XCreateColormap(gui->display, RootWindow(gui->display, screen),
                                    vi->visual, AllocNone);

    XSetWindowAttributes swa = {};
    swa.colormap = gui->colormap;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask | EnterWindowMask;

    gui->window = XCreateWindow(
        gui->display, parent_win,
        0, 0, gui->width, gui->height,
        0, vi->depth, InputOutput, vi->visual,
        CWColormap | CWEventMask, &swa
    );
    XFree(vi);

    if (!gui->window) {
        fprintf(stderr, "0xFX Plugin GUI: XCreateWindow failed\n");
        XFreeColormap(gui->display, gui->colormap);
        XCloseDisplay(gui->display);
        gui->display = NULL;
        return;
    }

    /* If parent was provided and isn't the root, reparent */
    if (parent_hwnd) {
        XReparentWindow(gui->display, gui->window,
                        (Window)(uintptr_t)parent_hwnd, 0, 0);
    }
    XMapWindow(gui->display, gui->window);
    XSync(gui->display, False);

    /* Create GLX context */
    gui->gl_ctx = glXCreateContext(gui->display, glXChooseVisual(gui->display, screen,
        attribs), NULL, GL_TRUE);
    if (!gui->gl_ctx) {
        fprintf(stderr, "0xFX Plugin GUI: glXCreateContext failed\n");
        XDestroyWindow(gui->display, gui->window);
        XFreeColormap(gui->display, gui->colormap);
        XCloseDisplay(gui->display);
        gui->display = NULL;
        return;
    }

    /* Release GL context from this thread */
    glXMakeCurrent(gui->display, None, NULL);

    gui->running = true;
    gui->visible = true;
    gui->thread_created = true;
    pthread_create(&gui->render_thread, NULL, render_thread_func, gui);

#elif defined(__APPLE__)
    /* macOS stub — not yet implemented */
    fprintf(stderr, "0xFX Plugin GUI: macOS native GUI not yet implemented\n");
    (void)parent_hwnd;
#endif
}

/* ─── Detach (stop render thread + destroy window) ─────────────────────── */

void oxfx_gui_detach(void *gui_ptr)
{
    gui_dbg("gui_detach gui=%p", gui_ptr);
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    if (!gui->running) return;

    gui->running = false;

#ifdef _WIN32
    if (gui->render_thread) {
        WaitForSingleObject(gui->render_thread, INFINITE);
        CloseHandle(gui->render_thread);
        gui->render_thread = NULL;
    }
    if (gui->hglrc) {
        wglDeleteContext(gui->hglrc);
        gui->hglrc = NULL;
    }
    if (gui->hdc && gui->hwnd) {
        ReleaseDC(gui->hwnd, gui->hdc);
        gui->hdc = NULL;
    }
    if (gui->hwnd) {
        DestroyWindow(gui->hwnd);
        gui->hwnd = NULL;
    }

#elif defined(__linux__)
    if (gui->thread_created) {
        pthread_join(gui->render_thread, NULL);
        gui->thread_created = false;
    }
    if (gui->gl_ctx && gui->display) {
        glXDestroyContext(gui->display, gui->gl_ctx);
        gui->gl_ctx = NULL;
    }
    if (gui->window && gui->display) {
        XDestroyWindow(gui->display, gui->window);
        gui->window = 0;
    }
    if (gui->colormap && gui->display) {
        XFreeColormap(gui->display, gui->colormap);
        gui->colormap = 0;
    }
    if (gui->display) {
        XCloseDisplay(gui->display);
        gui->display = NULL;
    }

#elif defined(__APPLE__)
    /* macOS stub */
#endif
}

void oxfx_gui_set_visible(void *gui_ptr, bool visible)
{
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    gui->visible = visible;

#ifdef _WIN32
    if (gui->hwnd) {
        ShowWindow(gui->hwnd, visible ? SW_SHOW : SW_HIDE);
    }
#elif defined(__linux__)
    if (gui->display && gui->window) {
        if (visible)
            XMapWindow(gui->display, gui->window);
        else
            XUnmapWindow(gui->display, gui->window);
        XFlush(gui->display);
    }
#endif
}

void oxfx_gui_get_size(void *gui_ptr, uint32_t *w, uint32_t *h)
{
    if (gui_ptr) {
        PluginGUI *gui = (PluginGUI *)gui_ptr;
        *w = gui->width;
        *h = gui->height;
    } else {
        *w = 1200;
        *h = 700;
    }
}

bool oxfx_gui_set_size(void *gui_ptr, uint32_t w, uint32_t h)
{
    if (!gui_ptr) return false;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    gui->width = w;
    gui->height = h;

#ifdef _WIN32
    if (gui->hwnd) {
        SetWindowPos(gui->hwnd, NULL, 0, 0, (int)w, (int)h,
                     SWP_NOZORDER | SWP_NOMOVE);
    }
#elif defined(__linux__)
    if (gui->display && gui->window) {
        XResizeWindow(gui->display, gui->window, w, h);
        XFlush(gui->display);
    }
#endif
    return true;
}

} /* extern "C" */
