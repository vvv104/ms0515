/*
 * DisplaySdl.cpp - Display through SDL2: the frontend's audio output and
 * its host-keyboard translation (ms0515_sdl_io), libapp's composition of
 * the screen, and a bare window - no menu, no settings.
 */

#include "Display.hpp"

#include <ms0515/app/Screen.hpp>

#define SDL_MAIN_HANDLED        /* main() is ours, not SDL's */
#include <Audio.hpp>
#include <PhysicalKeyboard.hpp>

#include <SDL.h>

namespace ms0515::run {

namespace {

/* The window is the screen twice over where the desktop has the room. */
int windowScale()
{
    SDL_DisplayMode mode{};
    if (SDL_GetDesktopDisplayMode(0, &mode) != 0)
        return 1;
    return mode.w >= 3 * app::kScreenWidth && mode.h >= 3 * app::kScreenHeight ? 2 : 1;
}

} /* namespace */

struct Display::Impl {
    std::string error;

    bool audioTried = false;
    bool audioOpen  = false;
    ms0515_frontend::Audio audio;

    SDL_Window   *window   = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture  *texture  = nullptr;
    app::Screen                       screen;
    ms0515_frontend::PhysicalKeyboard keyboard;
    uint32_t                          frames = 0;

    void closeWindow()
    {
        if (texture)  SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window)   SDL_DestroyWindow(window);
        texture = nullptr;
        renderer = nullptr;
        window = nullptr;
    }
};

Display::Display() : impl_(std::make_unique<Impl>())
{
    SDL_SetMainReady();
}

Display::~Display()
{
    impl_->closeWindow();
    impl_->audio.shutdown();
    SDL_Quit();
}

void Display::speaker(uint32_t cycle, int level)
{
    if (!impl_->audioTried) {
        impl_->audioTried = true;
        impl_->audioOpen = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 && impl_->audio.init();
    }
    impl_->audio.renderer().speaker(cycle, level);
}

bool Display::open(const std::string &title)
{
    if (impl_->window)
        return true;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        impl_->error = std::string{"no video: "} + SDL_GetError();
        return false;
    }
    const int scale = windowScale();
    impl_->window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED,
                                     app::kScreenWidth * scale,
                                     app::kScreenHeight * scale,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (impl_->window)
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_ACCELERATED);
    if (impl_->renderer)
        impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           app::kScreenWidth, app::kScreenHeight);
    if (!impl_->texture) {
        impl_->error = std::string{"no window: "} + SDL_GetError();
        impl_->closeWindow();
        return false;
    }
    /* The picture keeps its shape whatever the window is pulled to. */
    SDL_RenderSetLogicalSize(impl_->renderer, app::kScreenWidth, app::kScreenHeight);
    SDL_RaiseWindow(impl_->window);
    return true;
}

bool Display::isOpen() const noexcept
{
    return impl_->window != nullptr;
}

const std::string &Display::error() const noexcept
{
    return impl_->error;
}

bool Display::frame(ms0515::Emulator &emu)
{
    if (impl_->audioTried)
        impl_->audio.endFrame(static_cast<int>(emu.frameCyclePos()), impl_->audioOpen);
    if (!impl_->window)
        return true;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
            return false;
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            impl_->keyboard.handleEvent(event, emu, /*wantCapture=*/false);
    }

    impl_->screen.render(emu, impl_->frames++);
    SDL_UpdateTexture(impl_->texture, nullptr, impl_->screen.pixels(),
                      app::kScreenWidth * 4);
    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderClear(impl_->renderer);
    SDL_RenderCopy(impl_->renderer, impl_->texture, nullptr, nullptr);
    SDL_RenderPresent(impl_->renderer);
    return true;
}

} /* namespace ms0515::run */
