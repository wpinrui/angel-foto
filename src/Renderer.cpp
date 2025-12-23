#include "pch.h"
#include "Renderer.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

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
    swapChainDesc.Width = m_width > 0 ? m_width : 1;
    swapChainDesc.Height = m_height > 0 ? m_height : 1;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
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

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
    );

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

        D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
        );

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
    m_zoom = std::clamp(zoom, 0.1f, 10.0f);
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
            D2D1::ColorF(0, 0, 0, 0.5f), &m_cropDimBrush);
    }
}

void Renderer::SetCropRect(D2D1_RECT_F rect) {
    m_cropRect = rect;
}

void Renderer::SetMarkupStrokes(const std::vector<MarkupStroke>& strokes) {
    m_markupStrokes = strokes;
}

D2D1_RECT_F Renderer::GetScreenImageRect() {
    return CalculateImageRect();
}

D2D1_RECT_F Renderer::GetCropRectInImageCoords() const {
    if (!m_currentImage) return { 0, 0, 0, 0 };

    // Get the image rect in screen coordinates
    D2D1_RECT_F imageRect = const_cast<Renderer*>(this)->CalculateImageRect();

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

D2D1_RECT_F Renderer::CalculateImageRect() {
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
            auto imageSize = m_currentImage->GetSize();
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

        // Draw markup strokes
        for (const auto& stroke : m_markupStrokes) {
            if (stroke.points.size() < 2) continue;

            ComPtr<ID2D1SolidColorBrush> brush;
            m_deviceContext->CreateSolidColorBrush(stroke.color, &brush);
            if (!brush) continue;

            for (size_t i = 1; i < stroke.points.size(); ++i) {
                m_deviceContext->DrawLine(stroke.points[i - 1], stroke.points[i], brush.Get(), stroke.width);
            }
        }

        // Draw crop overlay if in crop mode
        if (m_cropMode && m_cropBrush && m_cropDimBrush) {
            // Dim area outside crop rect
            if (m_cropRect.right > m_cropRect.left && m_cropRect.bottom > m_cropRect.top) {
                // Top region
                m_deviceContext->FillRectangle(
                    D2D1::RectF(0, 0, static_cast<float>(m_width), m_cropRect.top),
                    m_cropDimBrush.Get());
                // Bottom region
                m_deviceContext->FillRectangle(
                    D2D1::RectF(0, m_cropRect.bottom, static_cast<float>(m_width), static_cast<float>(m_height)),
                    m_cropDimBrush.Get());
                // Left region
                m_deviceContext->FillRectangle(
                    D2D1::RectF(0, m_cropRect.top, m_cropRect.left, m_cropRect.bottom),
                    m_cropDimBrush.Get());
                // Right region
                m_deviceContext->FillRectangle(
                    D2D1::RectF(m_cropRect.right, m_cropRect.top, static_cast<float>(m_width), m_cropRect.bottom),
                    m_cropDimBrush.Get());

                // Draw crop border
                m_deviceContext->DrawRectangle(m_cropRect, m_cropBrush.Get(), 2.0f);
            }
        }
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
