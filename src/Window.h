#pragma once
#include "pch.h"

class App;

class Window {
public:
    Window(App* app);
    ~Window();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    HWND GetHwnd() const { return m_hwnd; }

    void SetTitle(const std::wstring& title);
    void ToggleFullscreen();
    bool IsFullscreen() const { return m_isFullscreen; }

    // Get client area size in pixels
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    float GetDpiScale() const { return m_dpiScale; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnResize(int width, int height);
    void OnDpiChanged(UINT dpi, const RECT* newRect);
    void ApplyDarkMode();

    App* m_app;
    HWND m_hwnd = nullptr;
    int m_width = 800;
    int m_height = 600;
    float m_dpiScale = 1.0f;
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_windowPlacement = { sizeof(WINDOWPLACEMENT) };
};
