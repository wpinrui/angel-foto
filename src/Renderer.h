#pragma once
#include "pch.h"

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Initialize(HWND hwnd);
    void Resize(int width, int height);
    void Render();

    // Set the current image to display
    void SetImage(ComPtr<ID2D1Bitmap> bitmap);
    void ClearImage();

    // Zoom and pan
    void SetZoom(float zoom);
    void SetPan(float panX, float panY);
    void AddPan(float dx, float dy);
    void ResetView();
    float GetZoom() const { return m_zoom; }
    float GetPanX() const { return m_panX; }
    float GetPanY() const { return m_panY; }

    // Rotation (0, 90, 180, 270)
    void SetRotation(int degrees);
    int GetRotation() const { return m_rotation; }

    // Crop mode
    void SetCropMode(bool enabled);
    void SetCropRect(D2D1_RECT_F rect);
    D2D1_RECT_F GetCropRectInImageCoords() const;

    // Markup strokes
    struct MarkupStroke {
        std::vector<D2D1_POINT_2F> points;
        D2D1_COLOR_F color;
        float width;
    };
    void SetMarkupStrokes(const std::vector<MarkupStroke>& strokes);

    // Text overlays
    struct TextOverlay {
        std::wstring text;
        float x, y;  // Normalized 0-1 coords
        D2D1_COLOR_F color;
        float fontSize;  // Normalized
    };
    void SetTextOverlays(const std::vector<TextOverlay>& overlays);

    // Get image rect in screen coordinates (for coordinate transforms)
    D2D1_RECT_F GetScreenImageRect() const;

    // Get Direct2D factory (for creating bitmaps)
    ID2D1Factory1* GetFactory() const { return m_factory.Get(); }
    ID2D1DeviceContext* GetDeviceContext() const { return m_deviceContext.Get(); }
    IWICImagingFactory* GetWICFactory() const { return m_wicFactory.Get(); }

private:
    void CreateDeviceResources();
    void DiscardDeviceResources();
    D2D1_RECT_F CalculateImageRect() const;

    HWND m_hwnd = nullptr;
    int m_width = 0;
    int m_height = 0;

    // Direct2D resources
    ComPtr<ID2D1Factory1> m_factory;
    ComPtr<ID2D1Device> m_device;
    ComPtr<ID2D1DeviceContext> m_deviceContext;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID2D1Bitmap1> m_targetBitmap;

    // WIC
    ComPtr<IWICImagingFactory> m_wicFactory;

    // Current image
    ComPtr<ID2D1Bitmap> m_currentImage;
    float m_zoom = 1.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    int m_rotation = 0;

    // Crop mode
    bool m_cropMode = false;
    D2D1_RECT_F m_cropRect = { 0, 0, 0, 0 };
    ComPtr<ID2D1SolidColorBrush> m_cropBrush;
    ComPtr<ID2D1SolidColorBrush> m_cropDimBrush;

    // Markup strokes
    std::vector<MarkupStroke> m_markupStrokes;
    ComPtr<ID2D1SolidColorBrush> m_markupBrush;

    // Text overlays
    std::vector<TextOverlay> m_textOverlays;
    ComPtr<IDWriteFactory> m_dwriteFactory;

    // Background color (dark)
    D2D1_COLOR_F m_backgroundColor = { 0.1f, 0.1f, 0.1f, 1.0f };

    // Zoom limits
    static constexpr float MIN_ZOOM = 0.1f;
    static constexpr float MAX_ZOOM = 10.0f;

    // Text rendering constants
    static constexpr wchar_t DEFAULT_FONT_NAME[] = L"Segoe UI";
    static constexpr wchar_t DEFAULT_LOCALE[] = L"en-us";
    static constexpr float CROP_DIM_OPACITY = 0.5f;
    static constexpr float CROP_BORDER_WIDTH = 2.0f;
};
