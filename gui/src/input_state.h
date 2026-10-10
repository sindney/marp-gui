#pragma once

#include "platform.h"
#include "TextEditor.h"
#include "imgui_internal.h"
#include <SDL3/SDL.h>
#include <functional>

#if PLATFORM_WINDOWS
#include <windows.h>
#include <imm.h>
#endif

namespace mg {

class InputState {
public:
    void Attach(SDL_Window* window, TextEditor& editor) {
        mEditor = &editor;
#if PLATFORM_WINDOWS
        sActive = this;
        mWindow = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (mWindow)
            mWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(mWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
        mImeCallback = ImGui::GetPlatformIO().Platform_SetImeDataFn;
        ImGui::GetPlatformIO().Platform_SetImeDataFn = ImeCallback;
#endif
    }

    void Detach() {
#if PLATFORM_WINDOWS
        if (mWindowProc) SetWindowLongPtrW(mWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(mWindowProc));
        ImGui::GetPlatformIO().Platform_SetImeDataFn = mImeCallback;
        sActive = nullptr;
#endif
    }

    void Event(const SDL_Event& event) {
        if (event.type == SDL_EVENT_TEXT_EDITING)
            mEditor->SetImeComposing(event.edit.text && *event.edit.text);
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            mEditor->SetImeComposing(false);
    }

    bool WantsFastPolling() const {
#if PLATFORM_WINDOWS
        bool recentActivity = mLastImeActivity && SDL_GetTicksNS() - mLastImeActivity < 100000000ULL;
        return mWindow && GetFocus() == mWindow &&
               (mEditor->IsImeComposing() || recentActivity);
#else
        return false;
#endif
    }

    static void ReleaseKeysNotHeld(const std::function<bool(int)>& isHeld) {
        struct Key { ImGuiKey imgui; int native; };
        static const Key keys[] = {
            {ImGuiKey_LeftArrow, 0x25}, {ImGuiKey_RightArrow, 0x27},
            {ImGuiKey_UpArrow, 0x26}, {ImGuiKey_DownArrow, 0x28},
            {ImGuiKey_Delete, 0x2e}, {ImGuiKey_Backspace, 0x08},
            {ImGuiKey_Enter, 0x0d}, {ImGuiKey_Tab, 0x09}, {ImGuiKey_Space, 0x20},
            {ImGuiKey_Home, 0x24}, {ImGuiKey_End, 0x23},
            {ImGuiKey_PageUp, 0x21}, {ImGuiKey_PageDown, 0x22}, {ImGuiKey_Insert, 0x2d},
            {ImGuiKey_LeftCtrl, 0xa2}, {ImGuiKey_RightCtrl, 0xa3},
            {ImGuiKey_LeftShift, 0xa0}, {ImGuiKey_RightShift, 0xa1},
            {ImGuiKey_LeftAlt, 0xa4}, {ImGuiKey_RightAlt, 0xa5},
            {ImGuiKey_LeftSuper, 0x5b}, {ImGuiKey_RightSuper, 0x5c}
        };
        auto check = [&](ImGuiKey key, int native) {
            const auto* data = ImGui::GetKeyData(key);
            if (data->Down && !isHeld(native))
                ImGui::GetIO().AddKeyEvent(key, false);
        };
        for (const auto& key : keys) check(key.imgui, key.native);
        for (int i = 0; i < 26; ++i) check((ImGuiKey)(ImGuiKey_A + i), 'A' + i);
        check((ImGuiKey)ImGuiMod_Ctrl, 0x11);
        check((ImGuiKey)ImGuiMod_Shift, 0x10);
        check((ImGuiKey)ImGuiMod_Alt, 0x12);
    }

private:
    TextEditor* mEditor = nullptr;
#if PLATFORM_WINDOWS
    void PositionCompositionWindow() {
        if (!mWindow || !mImeVisible) return;
        HIMC context = ImmGetContext(mWindow);
        if (!context) return;
        COMPOSITIONFORM current{};
        if (!ImmGetCompositionWindow(context, &current) ||
            current.dwStyle != mCompositionForm.dwStyle ||
            current.ptCurrentPos.x != mCompositionForm.ptCurrentPos.x ||
            current.ptCurrentPos.y != mCompositionForm.ptCurrentPos.y)
            ImmSetCompositionWindow(context, &mCompositionForm);
        ImmReleaseContext(mWindow, context);
    }

    static void ImeCallback(ImGuiContext* ctx, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
        auto& state = *sActive;
        if (state.mImeCallback) state.mImeCallback(ctx, viewport, data);
        state.mImeVisible = data->WantVisible;
        state.mCompositionForm = {};
        state.mCompositionForm.dwStyle = CFS_POINT;
        state.mCompositionForm.ptCurrentPos.x = (LONG)(data->InputPos.x - viewport->Pos.x);
        state.mCompositionForm.ptCurrentPos.y = (LONG)(data->InputPos.y - viewport->Pos.y + data->InputLineHeight);
        state.PositionCompositionWindow();
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto& state = *sActive;
        if (message >= WM_IME_STARTCOMPOSITION && message <= WM_IME_COMPOSITION)
            state.mLastImeActivity = SDL_GetTicksNS();
        if (message == WM_IME_STARTCOMPOSITION) state.mEditor->SetImeComposing(true);
        else if (message == WM_IME_ENDCOMPOSITION) state.mEditor->SetImeComposing(false);
        LRESULT result = CallWindowProcW(state.mWindowProc, hwnd, message, wparam, lparam);
        if (message == WM_IME_STARTCOMPOSITION || message == WM_INPUTLANGCHANGE)
            state.PositionCompositionWindow();
        return result;
    }
    inline static InputState* sActive = nullptr;
    HWND mWindow = nullptr;
    WNDPROC mWindowProc = nullptr;
    void (*mImeCallback)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*) = nullptr;
    COMPOSITIONFORM mCompositionForm{};
    bool mImeVisible = false;
    Uint64 mLastImeActivity = 0;
#endif
};

} // namespace mg
