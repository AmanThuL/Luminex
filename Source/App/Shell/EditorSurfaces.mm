//----------------------------------------------------------------------------------------------------------------------
/// @file EditorSurfaces.mm
/// @brief Owns the SDL Metal views behind detached editor viewport surfaces.
//----------------------------------------------------------------------------------------------------------------------

#include "App/Shell/EditorSurfaces.h"

#include "Core/Diagnostics/Assert.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <cstdint>
#include <format>
#include <map>

namespace lmx::app {
namespace {

struct SurfaceStore {
    std::map<void*, SDL_MetalView> views;
};

//======================================================================================================================
SurfaceStore& surfaceStore() {
    static SurfaceStore store;
    return store;
}

//======================================================================================================================
void* createSurface(ImGuiViewport* viewport, void* user) {
    LMX_ASSERT(viewport != nullptr, "editor surface creation needs a viewport");
    auto& store = *static_cast<SurfaceStore*>(user);
    const auto id =
        static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(viewport->PlatformHandle));
    SDL_Window* window = SDL_GetWindowFromID(id);
    LMX_ASSERT(window != nullptr, std::format("SDL_GetWindowFromID failed: {}", SDL_GetError()));
    SDL_MetalView view = SDL_Metal_CreateView(window);
    LMX_ASSERT(view != nullptr, std::format("SDL_Metal_CreateView failed: {}", SDL_GetError()));
    void* layer = SDL_Metal_GetLayer(view);
    LMX_ASSERT(layer != nullptr, std::format("SDL_Metal_GetLayer failed: {}", SDL_GetError()));
    const bool inserted = store.views.emplace(layer, view).second;
    LMX_ASSERT(inserted, "editor surface layer already has an owning SDL Metal view");
    return layer;
}

//======================================================================================================================
void destroySurface(ImGuiViewport*, void* surface, void* user) {
    auto& store = *static_cast<SurfaceStore*>(user);
    const auto found = store.views.find(surface);
    LMX_ASSERT(found != store.views.end(), "editor surface has no owning SDL Metal view");
    SDL_Metal_DestroyView(found->second);
    store.views.erase(found);
}

} // namespace

//======================================================================================================================
rojoRHI::imgui::SurfaceCallbacks editorSurfaceCallbacks() {
    return {createSurface, destroySurface, &surfaceStore()};
}

} // namespace lmx::app
