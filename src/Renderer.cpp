#include "pch.h"
#include "Renderer.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Helper to create render target bitmap properties (used by CreateDeviceResources and Resize)
static D2D1_BITMAP_PROPERTIES1 CreateRenderTargetBitmapProperties() {
    return D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
    );
}

Renderer::Renderer() {}

Renderer::~Renderer() {
    DiscardDeviceResources();
}

bool Renderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    RECT rc;
    GetClientRect(hwnd, &rc);
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;

    // Create WIC factory
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wicFactory)
    );
    if (FAILED(hr)) return false;

    // Create D2D factory
    D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        options,
        m_factory.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // Create DWrite factory for text rendering
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    try {
        CreateDeviceResources();
    } catch (...) {
        return false;
    }
    return true;
}

void Renderer::CreateDeviceResources() {
    // Create D3D11 device
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    D3D_FEATURE_LEVEL featureLevel;

    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &d3dDevice,
        &featureLevel,
        &d3dContext
    );

    if (FAILED(hr)) {
        // Fall back to WARP
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            creationFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &d3dDevice,
            &featureLevel,
            &d3dContext
        );
    }
    THROW_IF_FAILED(hr);

    // Get DXGI device
    ComPtr<IDXGIDevice1> dxgiDevice;
    hr = d3dDevice.As(&dxgiDevice);
    THROW_IF_FAILED(hr);

    // Create D2D device
    hr = m_factory->CreateDevice(dxgiDevice.Get(), &m_device);
    THROW_IF_FAILED(hr);

    // Create D2D device context
    hr = m_device->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &m_deviceContext
    );
    THROW_IF_FAILED(hr);

    // Get DXGI adapter and factory
    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    THROW_IF_FAILED(hr);

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    THROW_IF_FAILED(hr);

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_width > 0 ? m_width : MIN_DIMENSION;
    swapChainDesc.Height = m_height > 0 ? m_height : MIN_DIMENSION;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        d3dDevice.Get(),
        m_hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &m_swapChain
    );
    THROW_IF_FAILED(hr);

    // Create render target bitmap from swap chain
    ComPtr<IDXGISurface> dxgiSurface;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
    THROW_IF_FAILED(hr);

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = CreateRenderTargetBitmapProperties();

    hr = m_deviceContext->CreateBitmapFromDxgiSurface(
        dxgiSurface.Get(),
        bitmapProperties,
        &m_targetBitmap
    );
    THROW_IF_FAILED(hr);

    m_deviceContext->SetTarget(m_targetBitmap.Get());
}

void Renderer::DiscardDeviceResources() {
    m_targetBitmap.Reset();
    m_swapChain.Reset();
    m_deviceContext.Reset();
    m_device.Reset();
}

void Renderer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == m_width && height == m_height) return;
    if (!m_swapChain || !m_deviceContext) return;

    m_width = width;
    m_height = height;

    // Clear target before resizing
    m_deviceContext->SetTarget(nullptr);
    m_targetBitmap.Reset();

    // Resize swap chain
    HRESULT hr = m_swapChain->ResizeBuffers(
        0, width, height,
        DXGI_FORMAT_UNKNOWN, 0
    );

    if (SUCCEEDED(hr)) {
        // Recreate render target
        ComPtr<IDXGISurface> dxgiSurface;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
        THROW_IF_FAILED(hr);

        D2D1_BITMAP_PROPERTIES1 bitmapProperties = CreateRenderTargetBitmapProperties();

        hr = m_deviceContext->CreateBitmapFromDxgiSurface(
            dxgiSurface.Get(),
            bitmapProperties,
            &m_targetBitmap
        );
        THROW_IF_FAILED(hr);

        m_deviceContext->SetTarget(m_targetBitmap.Get());
    }
}

void Renderer::SetImage(ComPtr<ID2D1Bitmap> bitmap) {
    m_currentImage = bitmap;
    ResetView();
}

void Renderer::ClearImage() {
    m_currentImage.Reset();
}

void Renderer::SetZoom(float zoom) {
    m_zoom = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
}

void Renderer::SetPan(float panX, float panY) {
    m_panX = panX;
    m_panY = panY;
}

void Renderer::AddPan(float dx, float dy) {
    m_panX += dx;
    m_panY += dy;
}

void Renderer::ResetView() {
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
}

void Renderer::SetRotation(int degrees) {
    m_rotation = degrees % 360;
}

void Renderer::SetCropMode(bool enabled) {
    m_cropMode = enabled;
    if (!enabled) {
        m_cropRect = { 0, 0, 0, 0 };
    }

    // Create brushes for crop overlay if needed
    if (enabled && !m_cropBrush && m_deviceContext) {
        m_deviceContext->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White), &m_cropBrush);
        m_deviceContext->CreateSolidColorBrush(
            D2D1::ColorF(0, 0, 0, CROP_DIM_OPACITY), &m_cropDimBrush);
    }
}

void Renderer::SetCropRect(D2D1_RECT_F rect) {
    m_cropRect = rect;
}

void Renderer::SetMarkupStrokes(const std::vector<MarkupStroke>& strokes) {
    m_markupStrokes = strokes;
}

void Renderer::SetTextOverlays(const std::vector<TextOverlay>& overlays) {
    m_textOverlays = overlays;
}

D2D1_RECT_F Renderer::GetScreenImageRect() const {
    return CalculateImageRect();
}

D2D1_RECT_F Renderer::GetCropRectInImageCoords() const {
    if (!m_currentImage) return { 0, 0, 0, 0 };

    // Get the image rect in screen coordinates
    D2D1_RECT_F imageRect = CalculateImageRect();

    // Convert screen crop rect to image coordinates
    float scaleX = m_currentImage->GetSize().width / (imageRect.right - imageRect.left);
    float scaleY = m_currentImage->GetSize().height / (imageRect.bottom - imageRect.top);

    D2D1_RECT_F result;
    result.left = (m_cropRect.left - imageRect.left) * scaleX;
    result.top = (m_cropRect.top - imageRect.top) * scaleY;
    result.right = (m_cropRect.right - imageRect.left) * scaleX;
    result.bottom = (m_cropRect.bottom - imageRect.top) * scaleY;

    // Clamp to image bounds
    result.left = std::max(0.0f, std::min(result.left, m_currentImage->GetSize().width));
    result.top = std::max(0.0f, std::min(result.top, m_currentImage->GetSize().height));
    result.right = std::max(0.0f, std::min(result.right, m_currentImage->GetSize().width));
    result.bottom = std::max(0.0f, std::min(result.bottom, m_currentImage->GetSize().height));

    return result;
}

D2D1_RECT_F Renderer::CalculateImageRect() const {
    if (!m_currentImage) {
        return D2D1::RectF(0, 0, 0, 0);
    }

    auto imageSize = m_currentImage->GetSize();
    float imageWidth = imageSize.width;
    float imageHeight = imageSize.height;

    // Swap dimensions for 90/270 degree rotations
    if (m_rotation == 90 || m_rotation == 270) {
        std::swap(imageWidth, imageHeight);
    }

    // Calculate scale to fit image in window while preserving aspect ratio
    float scaleX = static_cast<float>(m_width) / imageWidth;
    float scaleY = static_cast<float>(m_height) / imageHeight;
    float fitScale = std::min(scaleX, scaleY);

    // Apply user zoom
    float finalScale = fitScale * m_zoom;

    // Calculate centered position
    float scaledWidth = imageWidth * finalScale;
    float scaledHeight = imageHeight * finalScale;
    float x = (m_width - scaledWidth) / 2.0f + m_panX;
    float y = (m_height - scaledHeight) / 2.0f + m_panY;

    return D2D1::RectF(x, y, x + scaledWidth, y + scaledHeight);
}

void Renderer::RenderMarkupStrokes(const D2D1_RECT_F& screenRect) {
    float screenW = screenRect.right - screenRect.left;
    float screenH = screenRect.bottom - screenRect.top;

    for (const auto& stroke : m_markupStrokes) {
        if (stroke.points.size() < 2) continue;

        ComPtr<ID2D1SolidColorBrush> brush;
        m_deviceContext->CreateSolidColorBrush(stroke.color, &brush);
        if (!brush) continue;

        float screenStrokeWidth = stroke.width * screenW;

        for (size_t i = 1; i < stroke.points.size(); ++i) {
            D2D1_POINT_2F p1 = {
                screenRect.left + stroke.points[i - 1].x * screenW,
                screenRect.top + stroke.points[i - 1].y * screenH
            };
            D2D1_POINT_2F p2 = {
                screenRect.left + stroke.points[i].x * screenW,
                screenRect.top + stroke.points[i].y * screenH
            };
            m_deviceContext->DrawLine(p1, p2, brush.Get(), screenStrokeWidth);
        }
    }
}

void Renderer::RenderTextOverlays(const D2D1_RECT_F& screenRect) {
    if (!m_dwriteFactory) return;

    float screenW = screenRect.right - screenRect.left;
    float screenH = screenRect.bottom - screenRect.top;

    for (const auto& text : m_textOverlays) {
        ComPtr<IDWriteTextFormat> textFormat;
        float screenFontSize = text.fontSize * screenW;
        m_dwriteFactory->CreateTextFormat(DEFAULT_FONT_NAME, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            screenFontSize, DEFAULT_LOCALE, &textFormat);
        if (!textFormat) continue;

        ComPtr<ID2D1SolidColorBrush> brush;
        m_deviceContext->CreateSolidColorBrush(text.color, &brush);
        if (!brush) continue;

        float screenX = screenRect.left + text.x * screenW;
        float screenY = screenRect.top + text.y * screenH;
        m_deviceContext->DrawText(text.text.c_str(), (UINT32)text.text.length(),
            textFormat.Get(), D2D1::RectF(screenX, screenY, screenX + TEXT_DRAW_MAX_WIDTH, screenY + TEXT_DRAW_MAX_HEIGHT), brush.Get());
    }
}

void Renderer::RenderCropOverlay() {
    if (!m_cropMode || !m_cropBrush || !m_cropDimBrush) return;
    if (m_cropRect.right <= m_cropRect.left || m_cropRect.bottom <= m_cropRect.top) return;

    float width = static_cast<float>(m_width);
    float height = static_cast<float>(m_height);

    // Dim area outside crop rect (top, bottom, left, right regions)
    m_deviceContext->FillRectangle(
        D2D1::RectF(0, 0, width, m_cropRect.top), m_cropDimBrush.Get());
    m_deviceContext->FillRectangle(
        D2D1::RectF(0, m_cropRect.bottom, width, height), m_cropDimBrush.Get());
    m_deviceContext->FillRectangle(
        D2D1::RectF(0, m_cropRect.top, m_cropRect.left, m_cropRect.bottom), m_cropDimBrush.Get());
    m_deviceContext->FillRectangle(
        D2D1::RectF(m_cropRect.right, m_cropRect.top, width, m_cropRect.bottom), m_cropDimBrush.Get());

    // Draw crop border
    m_deviceContext->DrawRectangle(m_cropRect, m_cropBrush.Get(), CROP_BORDER_WIDTH);
}

void Renderer::Render() {
    if (!m_deviceContext || !m_targetBitmap) return;

    m_deviceContext->BeginDraw();
    m_deviceContext->Clear(m_backgroundColor);

    if (m_currentImage) {
        D2D1_RECT_F destRect = CalculateImageRect();

        // Apply rotation transform
        if (m_rotation != 0) {
            float centerX = (destRect.left + destRect.right) / 2.0f;
            float centerY = (destRect.top + destRect.bottom) / 2.0f;
            m_deviceContext->SetTransform(
                D2D1::Matrix3x2F::Rotation(static_cast<float>(m_rotation),
                    D2D1::Point2F(centerX, centerY))
            );

            // Adjust destRect for rotated image
            if (m_rotation == 90 || m_rotation == 270) {
                float w = destRect.right - destRect.left;
                float h = destRect.bottom - destRect.top;
                destRect = D2D1::RectF(
                    centerX - h / 2, centerY - w / 2,
                    centerX + h / 2, centerY + w / 2
                );
            }
        }

        // Use high quality interpolation for better image quality
        m_deviceContext->DrawBitmap(
            m_currentImage.Get(),
            destRect,
            1.0f,
            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
        );

        // Reset transform
        m_deviceContext->SetTransform(D2D1::Matrix3x2F::Identity());

        // Render overlays using extracted helper methods
        D2D1_RECT_F screenRect = CalculateImageRect();
        RenderMarkupStrokes(screenRect);
        RenderTextOverlays(screenRect);
        RenderCropOverlay();
    }

    HRESULT hr = m_deviceContext->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        CreateDeviceResources();
    }

    // Present
    DXGI_PRESENT_PARAMETERS presentParams = {};
    m_swapChain->Present1(1, 0, &presentParams);
}
