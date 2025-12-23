#pragma once
#include "pch.h"
#include "Window.h"
#include "Renderer.h"
#include "ImageLoader.h"
#include "ImageCache.h"
#include "FolderNavigator.h"

class App {
public:
    App();
    ~App();

    bool Initialize(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile = L"");
    int Run();

    // Event handlers (called by Window)
    void OnKeyDown(UINT key);
    void OnKeyUp(UINT key);
    void OnMouseWheel(int delta);
    void OnMouseDown(int x, int y);
    void OnMouseUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnResize(int width, int height);
    void Render();

    // File operations
    void OpenFile(const std::wstring& filePath);

private:
    void LoadCurrentImage();
    void UpdateTitle();
    void NavigateNext();
    void NavigatePrevious();
    void NavigateFirst();
    void NavigateLast();
    void ToggleFullscreen();
    void DeleteCurrentFile();
    void ZoomIn();
    void ZoomOut();
    void ResetZoom();

    // Phase 2 features
    void CopyToClipboard();
    void SetAsWallpaper();
    void OpenFileDialog();
    void OpenFolderDialog();
    void SaveImage();
    void SaveImageAs();
    void RotateCW();
    void RotateCCW();
    void ToggleCropMode();
    void ToggleMarkupMode();
    void ToggleTextMode();
    void CancelCurrentMode();
    void ApplyCrop();

    // Image saving helper
    bool SaveImageToFile(const std::wstring& filePath);

    // Update renderer with current markup
    void UpdateRendererMarkup();

    // GIF animation
    void StartGifAnimation();
    void StopGifAnimation();
    void AdvanceGifFrame();
    static void CALLBACK GifTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<ImageLoader> m_imageLoader;
    std::unique_ptr<ImageCache> m_imageCache;
    std::unique_ptr<FolderNavigator> m_navigator;

    // Current image
    std::shared_ptr<ImageData> m_currentImage;

    // GIF animation
    UINT_PTR m_gifTimerId = 0;
    bool m_gifPaused = false;
    static App* s_instance; // For timer callback

    // Mouse state for panning
    bool m_isPanning = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    float m_panStartX = 0.0f;
    float m_panStartY = 0.0f;

    // Navigation key repeat handling
    bool m_isNavigating = false;
    DWORD m_lastNavigateTime = 0;
    static const DWORD NAVIGATE_DELAY_MS = 50; // Fast navigation when holding key

    // Rotation state (0, 90, 180, 270 degrees)
    int m_rotation = 0;

    // Edit modes
    enum class EditMode { None, Crop, Markup, Text };
    EditMode m_editMode = EditMode::None;

    // Crop selection
    bool m_isCropDragging = false;
    int m_cropStartX = 0;
    int m_cropStartY = 0;
    int m_cropEndX = 0;
    int m_cropEndY = 0;

    // Applied crop (in original image coordinates, before rotation)
    bool m_hasCrop = false;
    WICRect m_appliedCrop = {};

    // Markup drawing
    struct MarkupStroke {
        std::vector<D2D1_POINT_2F> points;
        D2D1_COLOR_F color;
        float width;
    };
    std::vector<MarkupStroke> m_markupStrokes;
    bool m_isDrawing = false;

    // Text overlay
    struct TextOverlay {
        std::wstring text;
        float x, y;
        D2D1_COLOR_F color;
        float fontSize;
    };
    std::vector<TextOverlay> m_textOverlays;
};
