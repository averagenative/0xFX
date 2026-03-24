/*
 * gui_plugin_bridge.cpp — Plugin GUI: SDL2 window embedded inside DAW host
 *
 * Creates an SDL2+OpenGL window, reparents it into the host-provided parent
 * window, and drives rendering on a dedicated thread using the shared
 * fx_gui_render_frame() from gui_render.h.
 *
 * Pattern copied from 0xSYNTH's plugin_gui.cpp (proven working).
 *
 * Platform-specific embedding:
 * - Windows: SetParent(sdl_hwnd, host_hwnd) + WS_CHILD + WndProc subclass
 * - Linux: SDL_ShowWindow (X11 reparenting — future)
 */

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "../gui/gui_render.h"
#include "../gui/texture.h"
#include "../engine/fx_engine.h"
}

#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_thread.h>
#include <SDL_syswm.h>

#include <cstdio>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>

/* Win32 WndProc subclass to capture keyboard + mouse events
 * that the DAW host doesn't forward to the child SDL window */
static WNDPROC s_orig_wndproc = NULL;

static LRESULT CALLBACK PluginWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        break; /* let SDL handle these normally */
    case WM_CHAR:
        return 0; /* consume — prevent Windows error beep */
    case WM_KEYDOWN:
    case WM_KEYUP:
        return 0; /* consume — prevent DAW accelerator interference */
    case WM_SYSCHAR:
        return 0; /* consume Alt+key beeps */
    default:
        break;
    }
    return CallWindowProcW(s_orig_wndproc, hwnd, msg, wp, lp);
}
#endif

/* ─── Plugin GUI State ───────────────────────────────────────────────────── */

struct PluginGUI {
    fx_engine_t     *engine;
    fx_gui_state_t  *gui_state;
    SDL_Window      *window;
    SDL_GLContext    gl_ctx;
    SDL_Thread      *render_thread;
    volatile bool    running;
    volatile bool    visible;
    uint32_t         width;
    uint32_t         height;
    void            *parent_handle;
    ImGuiContext    *imgui_ctx;
};

/* ─── Render Thread ──────────────────────────────────────────────────────── */

static int render_thread_func(void *data)
{
    PluginGUI *gui = (PluginGUI *)data;

    SDL_GL_MakeCurrent(gui->window, gui->gl_ctx);

    /* ImGui setup on render thread */
    IMGUI_CHECKVERSION();
    gui->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui->imgui_ctx);

    /* Disable imgui.ini — DAW working dirs are unpredictable */
    ImGui::GetIO().IniFilename = NULL;
    /* Suppress ID conflict popups in plugin — they're non-fatal */
    ImGui::GetIO().ConfigDebugHighlightIdConflicts = false;

    /* Apply the shared 0xFX "worn grime" theme */
    fx_gui_setup_theme();

    ImGui_ImplSDL2_InitForOpenGL(gui->window, gui->gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* Create the GUI rendering state (bound to the engine) */
    gui->gui_state = fx_gui_create(gui->engine);

    while (gui->running) {
        if (!gui->visible) {
            SDL_Delay(50);
            continue;
        }

        /* Process SDL events */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            /* Grab keyboard focus when mouse enters our window */
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_ENTER) {
                SDL_SetWindowInputFocus(gui->window);
            }
        }

        /* Track host window resize */
#ifdef _WIN32
        {
            SDL_SysWMinfo wi;
            SDL_VERSION(&wi.version);
            if (SDL_GetWindowWMInfo(gui->window, &wi)) {
                HWND parent = GetParent(wi.info.win.window);
                if (parent) {
                    RECT rc;
                    GetClientRect(parent, &rc);
                    int pw = rc.right - rc.left;
                    int ph = rc.bottom - rc.top;
                    if (pw > 0 && ph > 0 &&
                        ((uint32_t)pw != gui->width || (uint32_t)ph != gui->height)) {
                        gui->width = (uint32_t)pw;
                        gui->height = (uint32_t)ph;
                        SDL_SetWindowSize(gui->window, pw, ph);
                        SetWindowPos(wi.info.win.window, NULL, 0, 0, pw, ph,
                                     SWP_NOZORDER | SWP_NOMOVE);
                    }
                }
            }
        }

        /* Poll keyboard via GetAsyncKeyState — bypasses DAW accelerators */
        {
            ImGuiIO &io = ImGui::GetIO();
            POINT cursor;
            GetCursorPos(&cursor);
            SDL_SysWMinfo wi;
            SDL_VERSION(&wi.version);
            HWND our_hwnd = NULL;
            if (SDL_GetWindowWMInfo(gui->window, &wi))
                our_hwnd = wi.info.win.window;

            HWND under_cursor = WindowFromPoint(cursor);
            bool we_have_focus = (under_cursor == our_hwnd ||
                                  IsChild(our_hwnd, under_cursor));

            static bool prev_state[256] = {};

            if (we_have_focus && io.WantTextInput) {
                /* Text input mode: poll printable keys and inject chars */
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

                for (int vk = 'A'; vk <= 'Z'; vk++) {
                    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (down && !prev_state[vk]) {
                        char c = shift ? (char)vk : (char)(vk + 32);
                        io.AddInputCharacter((unsigned int)c);
                    }
                    prev_state[vk] = down;
                }
                for (int vk = '0'; vk <= '9'; vk++) {
                    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (down && !prev_state[vk]) {
                        io.AddInputCharacter((unsigned int)vk);
                    }
                    prev_state[vk] = down;
                }
                /* Space */
                {
                    bool down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
                    if (down && !prev_state[VK_SPACE])
                        io.AddInputCharacter(' ');
                    prev_state[VK_SPACE] = down;
                }
                /* Backspace */
                {
                    bool down = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
                    if (down && !prev_state[VK_BACK]) {
                        io.AddKeyEvent(ImGuiKey_Backspace, true);
                        io.AddKeyEvent(ImGuiKey_Backspace, false);
                    }
                    prev_state[VK_BACK] = down;
                }
                /* Enter */
                {
                    bool down = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
                    if (down && !prev_state[VK_RETURN]) {
                        io.AddKeyEvent(ImGuiKey_Enter, true);
                        io.AddKeyEvent(ImGuiKey_Enter, false);
                    }
                    prev_state[VK_RETURN] = down;
                }
                /* Common punctuation */
                static const struct { int vk; char normal; char shifted; } punct[] = {
                    {VK_OEM_MINUS, '-', '_'}, {VK_OEM_PLUS, '=', '+'},
                    {VK_OEM_PERIOD, '.', '>'}, {VK_OEM_COMMA, ',', '<'},
                    {0, 0, 0}
                };
                for (int p = 0; punct[p].vk; p++) {
                    bool down = (GetAsyncKeyState(punct[p].vk) & 0x8000) != 0;
                    if (down && !prev_state[punct[p].vk])
                        io.AddInputCharacter(shift ? punct[p].shifted : punct[p].normal);
                    prev_state[punct[p].vk] = down;
                }
            }
        }
#endif

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        /* Render the full 0xFX GUI via the shared renderer */
        fx_gui_render_frame(gui->gui_state, (float)gui->width, (float)gui->height,
                            true /* is_plugin */);

        ImGui::Render();

        glViewport(0, 0, (int)gui->width, (int)gui->height);
        glClearColor(0.06f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(gui->window);

        SDL_Delay(16); /* ~60fps */
    }

    /* Cleanup on render thread */
    if (gui->gui_state) {
        fx_gui_destroy(gui->gui_state);
        gui->gui_state = NULL;
    }

    /* Flush texture cache — GL IDs are invalid after context destruction */
    fx_texture_shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(gui->imgui_ctx);
    gui->imgui_ctx = NULL;

    return 0;
}

/* ─── Public C API ───────────────────────────────────────────────────────── */

extern "C" {

/* Forward declarations */
void oxfx_gui_detach(void *gui_ptr);

void *oxfx_gui_create(void *engine)
{
    PluginGUI *gui = new PluginGUI();
    memset(gui, 0, sizeof(*gui));
    gui->engine = (fx_engine_t *)engine;
    gui->width  = 1200;
    gui->height = 700;
    return gui;
}

void oxfx_gui_destroy(void *gui_ptr)
{
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    oxfx_gui_detach(gui_ptr);
    delete gui;
}

void oxfx_gui_attach(void *gui_ptr, void *parent_hwnd)
{
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    if (gui->running) return;

    gui->parent_handle = parent_hwnd;

    /* Only init SDL video if not already initialized (avoids conflict with 0x808/0xSYNTH) */
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "0xFX Plugin GUI: SDL_Init failed: %s\n", SDL_GetError());
            return;
        }
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);

    gui->window = SDL_CreateWindow(
        "0xFX",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        (int)gui->width, (int)gui->height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN
    );
    if (!gui->window) {
        fprintf(stderr, "0xFX Plugin GUI: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }

    gui->gl_ctx = SDL_GL_CreateContext(gui->window);
    if (!gui->gl_ctx) {
        fprintf(stderr, "0xFX Plugin GUI: GL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(gui->window);
        gui->window = NULL;
        return;
    }

    /* Reparent into host window (platform-specific) */
#ifdef _WIN32
    {
        SDL_SysWMinfo wm_info;
        SDL_VERSION(&wm_info.version);
        if (SDL_GetWindowWMInfo(gui->window, &wm_info)) {
            HWND sdl_hwnd = wm_info.info.win.window;
            HWND host_hwnd = (HWND)parent_hwnd;
            SetParent(sdl_hwnd, host_hwnd);
            LONG style = GetWindowLong(sdl_hwnd, GWL_STYLE);
            style = (style & ~WS_POPUP) | WS_CHILD;
            SetWindowLong(sdl_hwnd, GWL_STYLE, style);
            SetWindowPos(sdl_hwnd, NULL, 0, 0, (int)gui->width, (int)gui->height,
                         SWP_NOZORDER | SWP_FRAMECHANGED);
            ShowWindow(sdl_hwnd, SW_SHOW);

            /* Install WndProc hook for keyboard/mouse capture */
            s_orig_wndproc = (WNDPROC)SetWindowLongPtrW(
                sdl_hwnd, GWLP_WNDPROC, (LONG_PTR)PluginWndProc);
        }
    }
#else
    SDL_ShowWindow(gui->window);
#endif

    /* Release GL context from this thread before starting render thread */
    SDL_GL_MakeCurrent(gui->window, NULL);

    gui->running = true;
    gui->visible = true;
    gui->render_thread = SDL_CreateThread(render_thread_func, "oxfx_gui", gui);
}

void oxfx_gui_detach(void *gui_ptr)
{
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    if (!gui->running) return;

    gui->running = false;
    if (gui->render_thread) {
        SDL_WaitThread(gui->render_thread, NULL);
        gui->render_thread = NULL;
    }

#ifdef _WIN32
    /* Restore original WndProc before destroying window */
    if (gui->window && s_orig_wndproc) {
        SDL_SysWMinfo wi;
        SDL_VERSION(&wi.version);
        if (SDL_GetWindowWMInfo(gui->window, &wi)) {
            SetWindowLongPtrW(wi.info.win.window, GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
        }
        s_orig_wndproc = NULL;
    }
#endif

    if (gui->gl_ctx) {
        SDL_GL_DeleteContext(gui->gl_ctx);
        gui->gl_ctx = NULL;
    }
    if (gui->window) {
        SDL_DestroyWindow(gui->window);
        gui->window = NULL;
    }
}

void oxfx_gui_set_visible(void *gui_ptr, bool visible)
{
    if (!gui_ptr) return;
    PluginGUI *gui = (PluginGUI *)gui_ptr;
    gui->visible = visible;
    if (gui->window) {
        if (visible) SDL_ShowWindow(gui->window);
        else SDL_HideWindow(gui->window);
    }
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
    if (gui->window) {
        SDL_SetWindowSize(gui->window, (int)w, (int)h);
    }
    return true;
}

} /* extern "C" */
