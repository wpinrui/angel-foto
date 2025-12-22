#include "pch.h"
#include "App.h"

App* App::s_instance = nullptr;

App::App() {
    s_instance = this;
}

App::~App() {
    StopGifAnimation();
    if (m_imageCache) {
        m_imageCache->Shutdown();
    }
    s_instance = nullptr;
}

bool App::Initialize(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile) {
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        return false;
    }

    // Create components
    m_window = std::make_unique<Window>(this);
    m_renderer = std::make_unique<Renderer>();
    m_imageLoader = std::make_unique<ImageLoader>();
    m_imageCache = std::make_unique<ImageCache>();
    m_navigator = std::make_unique<FolderNavigator>();

    // Create window
    if (!m_window->Create(hInstance, nCmdShow)) {
        return false;
    }

    // Enable drag-drop
    DragAcceptFiles(m_window->GetHwnd(), TRUE);

    // Initialize renderer
    if (!m_renderer->Initialize(m_window->GetHwnd())) {
        return false;
    }

    // Initialize image loader
    m_imageLoader->Initialize(
        m_renderer->GetDeviceContext(),
        m_renderer->GetWICFactory()
    );

    // Initialize cache
    m_imageCache->Initialize(m_imageLoader.get());

    // Open initial file if provided
    if (!initialFile.empty()) {
        OpenFile(initialFile);
    }

    return true;
}

int App::Run() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::OpenFile(const std::wstring& filePath) {
    if (!ImageLoader::IsSupportedFormat(filePath)) {
        return;
    }

    m_navigator->SetCurrentFile(filePath);
    LoadCurrentImage();

    // Prefetch adjacent images
    auto adjacent = m_navigator->GetAdjacentFiles(3);
    m_imageCache->Prefetch(adjacent);
}

void App::LoadCurrentImage() {
    StopGifAnimation();

    std::wstring filePath = m_navigator->GetCurrentFilePath();
    if (filePath.empty()) {
        m_currentImage = nullptr;
        m_renderer->ClearImage();
        UpdateTitle();
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        return;
    }

    // Try cache first
    m_currentImage = m_imageCache->Get(filePath);

    if (!m_currentImage) {
        // Load synchronously
        m_currentImage = m_imageLoader->LoadImage(filePath);

        // Note: Could add to cache here, but for simplicity we skip it
    }

    if (m_currentImage) {
        m_renderer->SetImage(m_currentImage->bitmap);

        // Start animation if GIF
        if (m_currentImage->isAnimated) {
            StartGifAnimation();
        }
    } else {
        m_renderer->ClearImage();
    }

    UpdateTitle();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::UpdateTitle() {
    std::wstring title = L"angel-foto";

    if (m_currentImage && !m_currentImage->filePath.empty()) {
        fs::path path(m_currentImage->filePath);
        title = path.filename().wstring();

        // Add image info
        title += L" - " + std::to_wstring(m_currentImage->width) +
                 L" x " + std::to_wstring(m_currentImage->height);

        // Add position in folder
        title += L" [" + std::to_wstring(m_navigator->GetCurrentIndex() + 1) +
                 L"/" + std::to_wstring(m_navigator->GetTotalCount()) + L"]";

        // Add GIF pause indicator
        if (m_currentImage->isAnimated && m_gifPaused) {
            title += L" (paused)";
        }
    }

    m_window->SetTitle(title);
}

void App::NavigateNext() {
    if (m_navigator->GoToNext()) {
        LoadCurrentImage();

        // Prefetch next images
        auto adjacent = m_navigator->GetAdjacentFiles(3);
        m_imageCache->Prefetch(adjacent);
    }
}

void App::NavigatePrevious() {
    if (m_navigator->GoToPrevious()) {
        LoadCurrentImage();

        // Prefetch adjacent images
        auto adjacent = m_navigator->GetAdjacentFiles(3);
        m_imageCache->Prefetch(adjacent);
    }
}

void App::NavigateFirst() {
    if (m_navigator->GoToFirst()) {
        LoadCurrentImage();
    }
}

void App::NavigateLast() {
    if (m_navigator->GoToLast()) {
        LoadCurrentImage();
    }
}

void App::ToggleFullscreen() {
    m_window->ToggleFullscreen();
}

void App::DeleteCurrentFile() {
    if (m_navigator->DeleteCurrentFile()) {
        LoadCurrentImage();
    }
}

void App::ZoomIn() {
    float zoom = m_renderer->GetZoom();
    m_renderer->SetZoom(zoom * 1.25f);
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::ZoomOut() {
    float zoom = m_renderer->GetZoom();
    m_renderer->SetZoom(zoom / 1.25f);
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::ResetZoom() {
    m_renderer->ResetView();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::StartGifAnimation() {
    if (!m_currentImage || !m_currentImage->isAnimated) {
        return;
    }

    m_gifPaused = false;
    m_currentImage->currentFrame = 0;

    UINT delay = m_currentImage->frameDelays.empty() ? 100 : m_currentImage->frameDelays[0];
    m_gifTimerId = SetTimer(m_window->GetHwnd(), 1, delay, GifTimerProc);
}

void App::StopGifAnimation() {
    if (m_gifTimerId != 0) {
        KillTimer(m_window->GetHwnd(), m_gifTimerId);
        m_gifTimerId = 0;
    }
}

void App::AdvanceGifFrame() {
    if (!m_currentImage || !m_currentImage->isAnimated || m_gifPaused) {
        return;
    }

    // Advance to next frame
    m_currentImage->currentFrame++;
    if (m_currentImage->currentFrame >= m_currentImage->frames.size()) {
        m_currentImage->currentFrame = 0;
    }

    // Update bitmap
    if (m_currentImage->currentFrame < m_currentImage->frames.size()) {
        m_currentImage->bitmap = m_currentImage->frames[m_currentImage->currentFrame];
        m_renderer->SetImage(m_currentImage->bitmap);
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
    }

    // Schedule next frame
    UINT delay = 100;
    if (m_currentImage->currentFrame < m_currentImage->frameDelays.size()) {
        delay = m_currentImage->frameDelays[m_currentImage->currentFrame];
    }

    KillTimer(m_window->GetHwnd(), m_gifTimerId);
    m_gifTimerId = SetTimer(m_window->GetHwnd(), 1, delay, GifTimerProc);
}

void CALLBACK App::GifTimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    (void)hwnd; (void)msg; (void)id; (void)time;
    if (s_instance) {
        s_instance->AdvanceGifFrame();
    }
}

void App::OnKeyDown(UINT key) {
    DWORD now = GetTickCount();

    switch (key) {
    case VK_RIGHT:
        // Handle key repeat for fast navigation
        if (!m_isNavigating || (now - m_lastNavigateTime) >= NAVIGATE_DELAY_MS) {
            NavigateNext();
            m_isNavigating = true;
            m_lastNavigateTime = now;
        }
        break;

    case VK_LEFT:
        if (!m_isNavigating || (now - m_lastNavigateTime) >= NAVIGATE_DELAY_MS) {
            NavigatePrevious();
            m_isNavigating = true;
            m_lastNavigateTime = now;
        }
        break;

    case VK_HOME:
        NavigateFirst();
        break;

    case VK_END:
        NavigateLast();
        break;

    case VK_SPACE:
        // Toggle GIF pause
        if (m_currentImage && m_currentImage->isAnimated) {
            m_gifPaused = !m_gifPaused;
            UpdateTitle();
        }
        break;

    case VK_F11:
        ToggleFullscreen();
        break;

    case VK_DELETE:
        DeleteCurrentFile();
        break;

    case VK_ESCAPE:
        if (m_window->IsFullscreen()) {
            ToggleFullscreen();
        } else {
            PostQuitMessage(0);
        }
        break;

    case VK_OEM_PLUS:
    case VK_ADD:
        ZoomIn();
        break;

    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        ZoomOut();
        break;

    case 'F':
        ResetZoom();
        break;

    case '1':
        m_renderer->SetZoom(1.0f / (m_currentImage ?
            std::min(static_cast<float>(m_window->GetWidth()) / m_currentImage->width,
                     static_cast<float>(m_window->GetHeight()) / m_currentImage->height) : 1.0f));
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        break;
    }
}

void App::OnKeyUp(UINT key) {
    switch (key) {
    case VK_RIGHT:
    case VK_LEFT:
        m_isNavigating = false;
        break;
    }
}

void App::OnMouseWheel(int delta) {
    if (delta > 0) {
        ZoomIn();
    } else {
        ZoomOut();
    }
}

void App::OnMouseDown(int x, int y) {
    m_isPanning = true;
    m_lastMouseX = x;
    m_lastMouseY = y;
    SetCapture(m_window->GetHwnd());
}

void App::OnMouseUp(int x, int y) {
    (void)x; (void)y;
    m_isPanning = false;
    ReleaseCapture();
}

void App::OnMouseMove(int x, int y) {
    if (m_isPanning) {
        float dx = static_cast<float>(x - m_lastMouseX);
        float dy = static_cast<float>(y - m_lastMouseY);

        m_renderer->AddPan(dx, dy);

        m_lastMouseX = x;
        m_lastMouseY = y;

        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
    }
}

void App::OnResize(int width, int height) {
    if (m_renderer) {
        m_renderer->Resize(width, height);
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
    }
}

void App::Render() {
    if (m_renderer) {
        m_renderer->Render();
    }
}
