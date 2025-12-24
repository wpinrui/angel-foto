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

    // Reset all transformations when loading new image
    m_rotation = 0;
    m_renderer->SetRotation(0);
    m_hasCrop = false;
    m_appliedCrop = {};
    m_markupStrokes.clear();
    m_textOverlays.clear();
    m_undoStack.clear();

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

    // Add edit mode indicator
    switch (m_editMode) {
    case EditMode::Crop:
        title += L" [CROP - drag to select, Enter to apply, Esc to cancel]";
        break;
    case EditMode::Markup:
        title += L" [MARKUP - drag to draw, Esc to exit]";
        break;
    case EditMode::Text:
        title += L" [TEXT - click to add text, Esc to exit]";
        break;
    case EditMode::Erase:
        title += L" [ERASE - click on markup/text to delete, Esc to exit]";
        break;
    default:
        break;
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

// Phase 2 feature implementations

void App::CopyToClipboard() {
    if (!m_currentImage || !m_currentImage->bitmap) return;

    auto wicFactory = m_renderer->GetWICFactory();
    auto d2dFactory = m_renderer->GetFactory();
    if (!wicFactory || !d2dFactory) return;

    bool hasOverlays = !m_markupStrokes.empty() || !m_textOverlays.empty();

    // Load original image with WIC
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory->CreateDecoderFromFilename(
        m_currentImage->filePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return;

    ComPtr<IWICBitmapFrameDecode> frameDecode;
    hr = decoder->GetFrame(0, &frameDecode);
    if (FAILED(hr)) return;

    // Convert to 32bpp BGRA for clipboard
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return;

    hr = converter->Initialize(frameDecode.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return;

    ComPtr<IWICBitmapSource> source = converter;

    // Apply rotation if needed
    if (m_rotation != 0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        hr = wicFactory->CreateBitmapFlipRotator(&rotator);
        if (FAILED(hr)) return;

        WICBitmapTransformOptions transform = WICBitmapTransformRotate0;
        if (m_rotation == 90) transform = WICBitmapTransformRotate90;
        else if (m_rotation == 180) transform = WICBitmapTransformRotate180;
        else if (m_rotation == 270) transform = WICBitmapTransformRotate270;

        hr = rotator->Initialize(source.Get(), transform);
        if (FAILED(hr)) return;

        source = rotator;
    }

    // Apply crop if needed
    if (m_hasCrop) {
        ComPtr<IWICBitmapClipper> clipper;
        hr = wicFactory->CreateBitmapClipper(&clipper);
        if (FAILED(hr)) return;

        hr = clipper->Initialize(source.Get(), &m_appliedCrop);
        if (FAILED(hr)) return;

        source = clipper;
    }

    // Get final dimensions
    UINT width, height;
    source->GetSize(&width, &height);

    // Copy to buffer
    UINT stride = width * 4;
    std::vector<BYTE> buffer(stride * height);
    WICRect rcCopy = { 0, 0, (INT)width, (INT)height };
    hr = source->CopyPixels(&rcCopy, stride, (UINT)buffer.size(), buffer.data());
    if (FAILED(hr)) return;

    // Draw overlays if needed
    if (hasOverlays) {
        ComPtr<IWICBitmap> wicBitmap;
        hr = wicFactory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA,
            stride, (UINT)buffer.size(), buffer.data(), &wicBitmap);
        if (FAILED(hr)) return;

        ComPtr<ID2D1RenderTarget> rt;
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        hr = d2dFactory->CreateWicBitmapRenderTarget(wicBitmap.Get(), rtProps, &rt);
        if (FAILED(hr)) return;

        rt->BeginDraw();

        // Draw markup strokes
        float outW = static_cast<float>(width);
        float outH = static_cast<float>(height);

        for (const auto& stroke : m_markupStrokes) {
            if (stroke.points.size() < 2) continue;
            ComPtr<ID2D1SolidColorBrush> brush;
            rt->CreateSolidColorBrush(stroke.color, &brush);
            if (!brush) continue;

            float strokeWidth = stroke.width * outW;
            for (size_t i = 1; i < stroke.points.size(); ++i) {
                D2D1_POINT_2F p1 = { stroke.points[i-1].x * outW, stroke.points[i-1].y * outH };
                D2D1_POINT_2F p2 = { stroke.points[i].x * outW, stroke.points[i].y * outH };
                rt->DrawLine(p1, p2, brush.Get(), strokeWidth);
            }
        }

        // Draw text overlays
        ComPtr<IDWriteFactory> dwriteFactory;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf()));

        if (dwriteFactory) {
            for (const auto& text : m_textOverlays) {
                ComPtr<IDWriteTextFormat> textFormat;
                float fontSize = text.fontSize * outW;
                dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    fontSize, L"en-us", &textFormat);
                if (!textFormat) continue;

                ComPtr<ID2D1SolidColorBrush> brush;
                rt->CreateSolidColorBrush(text.color, &brush);
                if (!brush) continue;

                float x = text.x * outW;
                float y = text.y * outH;
                rt->DrawText(text.text.c_str(), (UINT32)text.text.length(),
                    textFormat.Get(), D2D1::RectF(x, y, x + 1000, y + 200), brush.Get());
            }
        }

        rt->EndDraw();

        // Copy back from WIC bitmap
        ComPtr<IWICBitmapLock> lock;
        WICRect lockRect = { 0, 0, (INT)width, (INT)height };
        hr = wicBitmap->Lock(&lockRect, WICBitmapLockRead, &lock);
        if (SUCCEEDED(hr)) {
            UINT bufSize;
            BYTE* pData;
            lock->GetDataPointer(&bufSize, &pData);
            memcpy(buffer.data(), pData, buffer.size());
        }
    }

    // Create DIB for clipboard
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -(int)height; // Top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + buffer.size());
    if (!hMem) return;

    void* pMem = GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return;
    }

    memcpy(pMem, &bi, sizeof(BITMAPINFOHEADER));
    memcpy(static_cast<BYTE*>(pMem) + sizeof(BITMAPINFOHEADER), buffer.data(), buffer.size());
    GlobalUnlock(hMem);

    if (OpenClipboard(m_window->GetHwnd())) {
        EmptyClipboard();
        SetClipboardData(CF_DIB, hMem);
        CloseClipboard();
    } else {
        GlobalFree(hMem);
    }
}

void App::SetAsWallpaper() {
    if (!m_currentImage || m_currentImage->filePath.empty()) return;

    // Copy original file to temp location (Windows wallpaper API works best with BMP/JPG)
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);

    fs::path srcPath(m_currentImage->filePath);
    std::wstring ext = srcPath.extension().wstring();
    std::wstring wallpaperPath = std::wstring(tempPath) + L"angel_foto_wallpaper" + ext;

    try {
        fs::copy_file(m_currentImage->filePath, wallpaperPath, fs::copy_options::overwrite_existing);
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0,
            const_cast<wchar_t*>(wallpaperPath.c_str()),
            SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    } catch (...) {
        // Copy failed
    }
}

void App::OpenFileDialog() {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return;

    COMDLG_FILTERSPEC filters[] = {
        { L"Image Files", L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.webp;*.heic;*.heif" },
        { L"All Files", L"*.*" }
    };
    dialog->SetFileTypes(ARRAYSIZE(filters), filters);

    hr = dialog->Show(m_window->GetHwnd());
    if (SUCCEEDED(hr)) {
        ComPtr<IShellItem> item;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr)) {
            PWSTR filePath;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
            if (SUCCEEDED(hr)) {
                OpenFile(filePath);
                CoTaskMemFree(filePath);
            }
        }
    }
}

void App::OpenFolderDialog() {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return;

    DWORD options;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS);

    hr = dialog->Show(m_window->GetHwnd());
    if (SUCCEEDED(hr)) {
        ComPtr<IShellItem> item;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr)) {
            PWSTR folderPath;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &folderPath);
            if (SUCCEEDED(hr)) {
                // Find first image in folder
                for (const auto& entry : fs::directory_iterator(folderPath)) {
                    if (ImageLoader::IsSupportedFormat(entry.path().wstring())) {
                        OpenFile(entry.path().wstring());
                        break;
                    }
                }
                CoTaskMemFree(folderPath);
            }
        }
    }
}

void App::SaveImage() {
    if (!m_currentImage || m_currentImage->filePath.empty()) return;

    fs::path origPath(m_currentImage->filePath);
    std::wstring savedFilePath = m_currentImage->filePath;
    bool saveCopy = false;

    // If there are edits (markups, text, crop), ask user what to do
    if (HasPendingEdits()) {
        TASKDIALOGCONFIG config = {};
        config.cbSize = sizeof(config);
        config.hwndParent = m_window->GetHwnd();
        config.dwFlags = TDF_USE_COMMAND_LINKS;
        config.pszWindowTitle = L"Save Image";
        config.pszMainIcon = TD_INFORMATION_ICON;
        config.pszMainInstruction = L"The image has markups or crop applied.";
        config.pszContent = L"How would you like to save?";

        TASKDIALOG_BUTTON buttons[] = {
            { 100, L"Save Copy\nOriginal file preserved" },
            { 101, L"Overwrite\nReplace original file" },
            { 102, L"Cancel\nDon't save" }
        };
        config.pButtons = buttons;
        config.cButtons = ARRAYSIZE(buttons);

        int clicked = 0;
        if (FAILED(TaskDialogIndirect(&config, &clicked, nullptr, nullptr))) return;

        if (clicked == 102) return;  // Cancel
        saveCopy = (clicked == 100);
    }

    if (saveCopy) {
        // Generate copy filename: image.jpg -> image_edited.jpg
        fs::path copyPath = origPath.parent_path() /
            (origPath.stem().wstring() + L"_edited" + origPath.extension().wstring());

        // If file exists, add number: image_edited_2.jpg
        int counter = 2;
        while (fs::exists(copyPath)) {
            copyPath = origPath.parent_path() /
                (origPath.stem().wstring() + L"_edited_" + std::to_wstring(counter++) + origPath.extension().wstring());
        }

        if (!SaveImageToFile(copyPath.wstring())) return;

        // Clear edits since they're now saved
        m_markupStrokes.clear();
        m_textOverlays.clear();
        m_hasCrop = false;
        m_appliedCrop = {};
        m_undoStack.clear();
        UpdateRendererMarkup();
        UpdateRendererText();
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);

        FlashWindow(m_window->GetHwnd(), TRUE);
    } else {
        // Overwrite original
        fs::path tempPath = origPath.parent_path() / (L"~temp_" + origPath.filename().wstring());

        // Save to temp file
        if (!SaveImageToFile(tempPath.wstring())) return;

        // Release current image so original file isn't locked
        m_currentImage->bitmap.Reset();
        m_currentImage = nullptr;
        m_renderer->ClearImage();

        // Replace original with temp
        try {
            fs::remove(origPath);
            fs::rename(tempPath, origPath);
        } catch (...) {
            try { fs::remove(tempPath); } catch (...) {}
        }

        // Reset transformations since they're now baked in
        m_rotation = 0;
        m_renderer->SetRotation(0);
        m_hasCrop = false;
        m_appliedCrop = {};
        m_markupStrokes.clear();
        m_textOverlays.clear();
        m_undoStack.clear();

        // Reload the image
        m_navigator->SetCurrentFile(savedFilePath);
        LoadCurrentImage();

        FlashWindow(m_window->GetHwnd(), TRUE);
    }
}

void App::SaveImageAs() {
    if (!m_currentImage || m_currentImage->filePath.empty()) return;

    ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return;

    // Get original extension
    fs::path srcPath(m_currentImage->filePath);
    std::wstring srcExt = srcPath.extension().wstring();

    COMDLG_FILTERSPEC filters[] = {
        { L"PNG Image", L"*.png" },
        { L"JPEG Image", L"*.jpg;*.jpeg" },
        { L"BMP Image", L"*.bmp" },
        { L"All Files", L"*.*" }
    };
    dialog->SetFileTypes(ARRAYSIZE(filters), filters);

    // Set default filter based on original extension
    std::wstring extLower = srcExt;
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);
    UINT filterIndex = 1; // Default to PNG
    if (extLower == L".jpg" || extLower == L".jpeg") filterIndex = 2;
    else if (extLower == L".bmp") filterIndex = 3;
    else if (extLower == L".png") filterIndex = 1;
    dialog->SetFileTypeIndex(filterIndex);

    dialog->SetDefaultExtension(srcExt.empty() ? L"png" : srcExt.c_str() + 1);
    dialog->SetFileName(srcPath.stem().wstring().c_str());

    hr = dialog->Show(m_window->GetHwnd());
    if (SUCCEEDED(hr)) {
        ComPtr<IShellItem> item;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr)) {
            PWSTR filePath;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
            if (SUCCEEDED(hr)) {
                // Save with transformations applied
                SaveImageToFile(filePath);
                CoTaskMemFree(filePath);
            }
        }
    }
}

bool App::SaveImageToFile(const std::wstring& filePath) {
    if (!m_currentImage || m_currentImage->filePath.empty()) return false;

    auto wicFactory = m_renderer->GetWICFactory();
    auto d2dFactory = m_renderer->GetFactory();
    if (!wicFactory || !d2dFactory) return false;

    // Determine output format
    fs::path path(filePath);
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    GUID containerFormat;
    if (ext == L".jpg" || ext == L".jpeg") {
        containerFormat = GUID_ContainerFormatJpeg;
    } else if (ext == L".bmp") {
        containerFormat = GUID_ContainerFormatBmp;
    } else {
        containerFormat = GUID_ContainerFormatPng;
    }

    bool hasOverlays = !m_markupStrokes.empty() || !m_textOverlays.empty();

    // Use 32bppPBGRA (premultiplied) for D2D rendering when we have overlays
    WICPixelFormatGUID targetPixelFormat = hasOverlays ?
        GUID_WICPixelFormat32bppPBGRA :
        ((containerFormat == GUID_ContainerFormatJpeg || containerFormat == GUID_ContainerFormatBmp) ?
            GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA);

    // Load original image with WIC
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory->CreateDecoderFromFilename(
        m_currentImage->filePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frameDecode;
    hr = decoder->GetFrame(0, &frameDecode);
    if (FAILED(hr)) return false;

    // Convert to target format
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;

    hr = converter->Initialize(frameDecode.Get(), targetPixelFormat,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapSource> source = converter;

    // Apply rotation if needed
    if (m_rotation != 0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        hr = wicFactory->CreateBitmapFlipRotator(&rotator);
        if (FAILED(hr)) return false;

        WICBitmapTransformOptions transform = WICBitmapTransformRotate0;
        if (m_rotation == 90) transform = WICBitmapTransformRotate90;
        else if (m_rotation == 180) transform = WICBitmapTransformRotate180;
        else if (m_rotation == 270) transform = WICBitmapTransformRotate270;

        hr = rotator->Initialize(source.Get(), transform);
        if (FAILED(hr)) return false;

        source = rotator;
    }

    // Apply crop if needed
    if (m_hasCrop) {
        ComPtr<IWICBitmapClipper> clipper;
        hr = wicFactory->CreateBitmapClipper(&clipper);
        if (FAILED(hr)) return false;

        hr = clipper->Initialize(source.Get(), &m_appliedCrop);
        if (FAILED(hr)) return false;

        source = clipper;
    }

    // Get final dimensions
    UINT width, height;
    source->GetSize(&width, &height);

    // Always materialize to a WIC bitmap to avoid streaming issues
    UINT bpp = (targetPixelFormat == GUID_WICPixelFormat32bppBGRA || targetPixelFormat == GUID_WICPixelFormat32bppPBGRA) ? 4 : 3;
    UINT stride = ((width * bpp) + 3) & ~3;  // DWORD-aligned stride
    std::vector<BYTE> buffer(stride * height);

    WICRect rcCopy = { 0, 0, (INT)width, (INT)height };
    hr = source->CopyPixels(&rcCopy, stride, (UINT)buffer.size(), buffer.data());
    if (FAILED(hr)) return false;

    // Create WIC bitmap from buffer
    ComPtr<IWICBitmap> wicBitmap;
    hr = wicFactory->CreateBitmapFromMemory(width, height, targetPixelFormat,
        stride, (UINT)buffer.size(), buffer.data(), &wicBitmap);
    if (FAILED(hr)) return false;

    // Draw overlays if needed (strokes are in normalized 0-1 coords)
    if (hasOverlays) {
        ComPtr<ID2D1RenderTarget> rt;
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        hr = d2dFactory->CreateWicBitmapRenderTarget(wicBitmap.Get(), rtProps, &rt);
        if (FAILED(hr)) return false;

        rt->BeginDraw();

        // Draw markup strokes (normalized coords scaled to output size)
        float outW = static_cast<float>(width);
        float outH = static_cast<float>(height);

        for (const auto& stroke : m_markupStrokes) {
            if (stroke.points.size() < 2) continue;
            ComPtr<ID2D1SolidColorBrush> brush;
            rt->CreateSolidColorBrush(stroke.color, &brush);
            if (!brush) continue;

            // Stroke width is normalized, scale to output width
            float strokeWidth = stroke.width * outW;

            for (size_t i = 1; i < stroke.points.size(); ++i) {
                // Scale normalized coords to output pixel coords
                D2D1_POINT_2F p1 = {
                    stroke.points[i - 1].x * outW,
                    stroke.points[i - 1].y * outH
                };
                D2D1_POINT_2F p2 = {
                    stroke.points[i].x * outW,
                    stroke.points[i].y * outH
                };
                rt->DrawLine(p1, p2, brush.Get(), strokeWidth);
            }
        }

        // Draw text overlays (normalized 0-1 coords scaled to output size)
        ComPtr<IDWriteFactory> dwriteFactory;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf()));
        if (dwriteFactory) {
            for (const auto& text : m_textOverlays) {
                ComPtr<IDWriteTextFormat> textFormat;
                float scaledFontSize = text.fontSize * outW;
                dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    scaledFontSize, L"en-us", &textFormat);
                if (!textFormat) continue;
                ComPtr<ID2D1SolidColorBrush> brush;
                rt->CreateSolidColorBrush(text.color, &brush);
                if (!brush) continue;
                float tx = text.x * outW;
                float ty = text.y * outH;
                rt->DrawText(text.text.c_str(), (UINT32)text.text.length(), textFormat.Get(),
                    D2D1::RectF(tx, ty, outW, outH), brush.Get());
            }
        }

        hr = rt->EndDraw();
        if (FAILED(hr)) return false;
    }

    // Create encoder
    ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICStream> stream;
    hr = wicFactory->CreateStream(&stream);
    if (FAILED(hr)) return false;

    hr = stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) return false;

    // Set JPEG quality
    if (containerFormat == GUID_ContainerFormatJpeg && props) {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = 0.9f;
        props->Write(1, &option, &value);
    }

    hr = frame->Initialize(props.Get());
    if (FAILED(hr)) return false;

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) return false;

    WICPixelFormatGUID pixelFormat = targetPixelFormat;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) return false;

    hr = frame->WriteSource(wicBitmap.Get(), nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    return SUCCEEDED(hr);
}

void App::RotateCW() {
    if (!m_currentImage || m_currentImage->filePath.empty()) return;

    m_rotation = (m_rotation + 90) % 360;
    m_renderer->SetRotation(m_rotation);
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);

    // Auto-save rotation (rotation is reversible by rotating back)
    fs::path origPath(m_currentImage->filePath);
    fs::path tempPath = origPath.parent_path() / (L"~temp_" + origPath.filename().wstring());
    std::wstring savedFilePath = m_currentImage->filePath;

    // Save current state (without markups for rotation-only save)
    auto savedStrokes = m_markupStrokes;
    auto savedTexts = m_textOverlays;
    bool savedHasCrop = m_hasCrop;
    auto savedCrop = m_appliedCrop;
    m_markupStrokes.clear();
    m_textOverlays.clear();
    m_hasCrop = false;
    m_appliedCrop = {};

    if (SaveImageToFile(tempPath.wstring())) {
        m_currentImage->bitmap.Reset();
        m_currentImage = nullptr;
        m_renderer->ClearImage();

        try {
            fs::remove(origPath);
            fs::rename(tempPath, origPath);
        } catch (...) {
            try { fs::remove(tempPath); } catch (...) {}
        }

        m_rotation = 0;
        m_renderer->SetRotation(0);
        m_navigator->SetCurrentFile(savedFilePath);
        LoadCurrentImage();
    }

    // Restore markups/crop state
    m_markupStrokes = savedStrokes;
    m_textOverlays = savedTexts;
    m_hasCrop = savedHasCrop;
    m_appliedCrop = savedCrop;
    UpdateRendererMarkup();
    UpdateRendererText();
}

void App::RotateCCW() {
    if (!m_currentImage || m_currentImage->filePath.empty()) return;

    m_rotation = (m_rotation + 270) % 360;
    m_renderer->SetRotation(m_rotation);
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);

    // Auto-save rotation (rotation is reversible by rotating back)
    fs::path origPath(m_currentImage->filePath);
    fs::path tempPath = origPath.parent_path() / (L"~temp_" + origPath.filename().wstring());
    std::wstring savedFilePath = m_currentImage->filePath;

    // Save current state (without markups for rotation-only save)
    auto savedStrokes = m_markupStrokes;
    auto savedTexts = m_textOverlays;
    bool savedHasCrop = m_hasCrop;
    auto savedCrop = m_appliedCrop;
    m_markupStrokes.clear();
    m_textOverlays.clear();
    m_hasCrop = false;
    m_appliedCrop = {};

    if (SaveImageToFile(tempPath.wstring())) {
        m_currentImage->bitmap.Reset();
        m_currentImage = nullptr;
        m_renderer->ClearImage();

        try {
            fs::remove(origPath);
            fs::rename(tempPath, origPath);
        } catch (...) {
            try { fs::remove(tempPath); } catch (...) {}
        }

        m_rotation = 0;
        m_renderer->SetRotation(0);
        m_navigator->SetCurrentFile(savedFilePath);
        LoadCurrentImage();
    }

    // Restore markups/crop state
    m_markupStrokes = savedStrokes;
    m_textOverlays = savedTexts;
    m_hasCrop = savedHasCrop;
    m_appliedCrop = savedCrop;
    UpdateRendererMarkup();
    UpdateRendererText();
}

void App::ToggleCropMode() {
    if (m_editMode == EditMode::Crop) {
        m_editMode = EditMode::None;
    } else {
        m_editMode = EditMode::Crop;
        m_isCropDragging = false;
    }
    m_renderer->SetCropMode(m_editMode == EditMode::Crop);
    UpdateTitle();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::ToggleMarkupMode() {
    if (m_editMode == EditMode::Markup) {
        m_editMode = EditMode::None;
    } else {
        m_editMode = EditMode::Markup;
    }
    UpdateTitle();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::ToggleTextMode() {
    if (m_editMode == EditMode::Text) {
        m_editMode = EditMode::None;
    } else {
        m_editMode = EditMode::Text;
    }
    UpdateTitle();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::ToggleEraseMode() {
    if (m_editMode == EditMode::Erase) {
        m_editMode = EditMode::None;
    } else {
        m_editMode = EditMode::Erase;
    }
    UpdateTitle();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::CancelCurrentMode() {
    m_editMode = EditMode::None;
    m_isCropDragging = false;
    m_renderer->SetCropMode(false);
    m_renderer->SetCropRect(D2D1::RectF(0, 0, 0, 0));
    UpdateTitle();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

void App::UpdateRendererMarkup() {
    std::vector<Renderer::MarkupStroke> rendererStrokes;
    for (const auto& stroke : m_markupStrokes) {
        Renderer::MarkupStroke rs;
        rs.points = stroke.points;
        rs.color = stroke.color;
        rs.width = stroke.width;
        rendererStrokes.push_back(rs);
    }
    m_renderer->SetMarkupStrokes(rendererStrokes);
}

void App::UpdateRendererText() {
    std::vector<Renderer::TextOverlay> rendererTexts;
    for (const auto& text : m_textOverlays) {
        Renderer::TextOverlay rt;
        rt.text = text.text;
        rt.x = text.x;
        rt.y = text.y;
        rt.color = text.color;
        rt.fontSize = text.fontSize;
        rendererTexts.push_back(rt);
    }

    // Add editing text with cursor
    if (m_isEditingText) {
        D2D1_RECT_F imageRect = m_renderer->GetScreenImageRect();
        float imageW = imageRect.right - imageRect.left;

        Renderer::TextOverlay rt;
        rt.text = m_editingText + L"|";  // Cursor character
        rt.x = m_editingTextX;
        rt.y = m_editingTextY;
        rt.color = D2D1::ColorF(D2D1::ColorF::White);
        rt.fontSize = 24.0f / imageW;
        rendererTexts.push_back(rt);
    }

    m_renderer->SetTextOverlays(rendererTexts);
}

void App::EraseAtPoint(int x, int y) {
    D2D1_RECT_F imageRect = m_renderer->GetScreenImageRect();
    float imageW = imageRect.right - imageRect.left;
    float imageH = imageRect.bottom - imageRect.top;

    if (imageW <= 0 || imageH <= 0) return;

    float normX = (static_cast<float>(x) - imageRect.left) / imageW;
    float normY = (static_cast<float>(y) - imageRect.top) / imageH;
    float hitRadius = 30.0f / std::min(imageW, imageH);

    bool erased = false;

    // Check strokes for hit - erase any stroke that intersects
    for (auto it = m_markupStrokes.begin(); it != m_markupStrokes.end(); ) {
        bool hit = false;
        for (const auto& pt : it->points) {
            float dx = pt.x - normX;
            float dy = pt.y - normY;
            if (dx * dx + dy * dy < hitRadius * hitRadius) {
                hit = true;
                break;
            }
        }
        if (hit) {
            it = m_markupStrokes.erase(it);
            erased = true;
        } else {
            ++it;
        }
    }

    // Check text overlays for hit
    for (auto it = m_textOverlays.begin(); it != m_textOverlays.end(); ) {
        float dx = it->x - normX;
        float dy = it->y - normY;
        // Text hit box - check if click is near text origin
        if (dx > -hitRadius && dx < 0.2f && dy > -hitRadius && dy < hitRadius * 2) {
            it = m_textOverlays.erase(it);
            erased = true;
        } else {
            ++it;
        }
    }

    if (erased) {
        UpdateRendererMarkup();
        UpdateRendererText();
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
    }
}

void App::PushUndoState() {
    EditState state;
    state.strokes = m_markupStrokes;
    state.texts = m_textOverlays;
    state.hasCrop = m_hasCrop;
    state.appliedCrop = m_appliedCrop;
    m_undoStack.push_back(state);

    // Limit undo stack size
    if (m_undoStack.size() > MAX_UNDO_LEVELS) {
        m_undoStack.erase(m_undoStack.begin());
    }
}

void App::Undo() {
    if (m_undoStack.empty()) return;

    EditState state = m_undoStack.back();
    m_undoStack.pop_back();

    // Check if we're undoing a crop (need to reload image)
    bool wasCopped = m_hasCrop;
    bool willBeCropped = state.hasCrop;

    m_markupStrokes = state.strokes;
    m_textOverlays = state.texts;
    m_hasCrop = state.hasCrop;
    m_appliedCrop = state.appliedCrop;

    // If undoing a crop, reload the original image
    if (wasCopped && !willBeCropped && m_currentImage) {
        std::wstring filePath = m_currentImage->filePath;
        m_currentImage = m_imageLoader->LoadImage(filePath);
        if (m_currentImage) {
            m_renderer->SetImage(m_currentImage->bitmap);
        }
    }

    UpdateRendererMarkup();
    UpdateRendererText();
    InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
}

bool App::HasPendingEdits() const {
    return !m_markupStrokes.empty() || !m_textOverlays.empty() || m_hasCrop;
}

void App::ApplyCrop() {
    if (m_editMode != EditMode::Crop || !m_currentImage) return;

    // Get crop rect in image coordinates
    D2D1_RECT_F cropRect = m_renderer->GetCropRectInImageCoords();
    if (cropRect.right <= cropRect.left || cropRect.bottom <= cropRect.top) return;

    int cropX = static_cast<int>(cropRect.left);
    int cropY = static_cast<int>(cropRect.top);
    int cropW = static_cast<int>(cropRect.right - cropRect.left);
    int cropH = static_cast<int>(cropRect.bottom - cropRect.top);

    if (cropW <= 0 || cropH <= 0) return;

    // Store crop for saving
    m_hasCrop = true;
    m_appliedCrop = { cropX, cropY, cropW, cropH };

    auto dc = m_renderer->GetDeviceContext();

    // Create cropped bitmap for display
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    ComPtr<ID2D1Bitmap1> croppedBitmap;
    HRESULT hr = dc->CreateBitmap(D2D1::SizeU(cropW, cropH), nullptr, 0, props, &croppedBitmap);
    if (FAILED(hr)) return;

    D2D1_POINT_2U destPoint = {0, 0};
    D2D1_RECT_U srcRect = {(UINT32)cropX, (UINT32)cropY, (UINT32)(cropX + cropW), (UINT32)(cropY + cropH)};
    hr = croppedBitmap->CopyFromBitmap(&destPoint, m_currentImage->bitmap.Get(), &srcRect);
    if (FAILED(hr)) return;

    // Update current image
    m_currentImage->bitmap = croppedBitmap;
    m_currentImage->width = cropW;
    m_currentImage->height = cropH;

    m_renderer->SetImage(m_currentImage->bitmap);
    CancelCurrentMode();
}

void App::OnKeyDown(UINT key) {
    // When editing text, only handle Escape, Enter, and Backspace
    if (m_isEditingText) {
        if (key != VK_ESCAPE && key != VK_RETURN && key != VK_BACK) {
            return;  // Let OnChar handle printable characters
        }
    }

    DWORD now = GetTickCount();
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

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
        if (m_isEditingText) {
            // Cancel text editing
            m_isEditingText = false;
            m_editingText.clear();
            UpdateRendererText();
            InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        } else if (m_editMode != EditMode::None) {
            CancelCurrentMode();
        } else if (m_window->IsFullscreen()) {
            ToggleFullscreen();
        } else {
            PostQuitMessage(0);
        }
        break;

    case VK_RETURN:
        if (m_isEditingText) {
            // Commit the text
            if (!m_editingText.empty()) {
                PushUndoState();
                D2D1_RECT_F imageRect = m_renderer->GetScreenImageRect();
                float imageW = imageRect.right - imageRect.left;

                TextOverlay text;
                text.x = m_editingTextX;
                text.y = m_editingTextY;
                text.text = m_editingText;
                text.color = D2D1::ColorF(D2D1::ColorF::White);
                text.fontSize = 24.0f / imageW;
                m_textOverlays.push_back(text);
            }
            m_isEditingText = false;
            m_editingText.clear();
            UpdateRendererText();
            InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        } else if (m_editMode == EditMode::Crop) {
            PushUndoState();
            ApplyCrop();
        }
        break;

    case VK_BACK:
        if (m_isEditingText && !m_editingText.empty()) {
            m_editingText.pop_back();
            UpdateRendererText();
            InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
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
        if (ctrl) {
            OpenFolderDialog();
        } else {
            ResetZoom();
        }
        break;

    case '1':
        m_renderer->SetZoom(1.0f / (m_currentImage ?
            std::min(static_cast<float>(m_window->GetWidth()) / m_currentImage->width,
                     static_cast<float>(m_window->GetHeight()) / m_currentImage->height) : 1.0f));
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        break;

    // Phase 2 shortcuts
    case 'C':
        if (ctrl) {
            CopyToClipboard();
        } else {
            ToggleCropMode();
        }
        break;

    case 'B':
        if (ctrl) SetAsWallpaper();
        break;

    case 'O':
        if (ctrl) OpenFileDialog();
        break;

    case 'S':
        if (ctrl && shift) {
            SaveImageAs();
        } else if (ctrl) {
            SaveImage();
        }
        break;

    case 'R':
        if (shift) {
            RotateCCW();
        } else {
            RotateCW();
        }
        break;

    case 'M':
        ToggleMarkupMode();
        break;

    case 'T':
        ToggleTextMode();
        break;

    case 'E':
        ToggleEraseMode();
        break;

    case 'Z':
        if (ctrl) Undo();
        break;

    case 'Q':
        if (ctrl) PostQuitMessage(0);
        break;

    case 'W':
        if (ctrl) {
            // Close current image
            m_currentImage = nullptr;
            m_renderer->ClearImage();
            m_navigator->Clear();
            UpdateTitle();
            InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        }
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

void App::OnChar(wchar_t ch) {
    if (m_isEditingText) {
        // Ignore control characters (handled by OnKeyDown)
        if (ch >= 32) {  // Printable characters
            m_editingText += ch;
            UpdateRendererText();
            InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        }
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
    if (m_editMode == EditMode::Crop) {
        m_isCropDragging = true;
        m_cropStartX = x;
        m_cropStartY = y;
        m_cropEndX = x;
        m_cropEndY = y;
        SetCapture(m_window->GetHwnd());
    } else if (m_editMode == EditMode::Markup) {
        // Convert screen coords to normalized image coords (0-1 range)
        D2D1_RECT_F imageRect = m_renderer->GetScreenImageRect();
        float imageW = imageRect.right - imageRect.left;
        float imageH = imageRect.bottom - imageRect.top;
        if (imageW > 0 && imageH > 0) {
            float normX = (static_cast<float>(x) - imageRect.left) / imageW;
            float normY = (static_cast<float>(y) - imageRect.top) / imageH;

            PushUndoState();
            m_isDrawing = true;
            MarkupStroke stroke;
            stroke.color = D2D1::ColorF(D2D1::ColorF::Red);
            stroke.width = 3.0f / imageW;  // Normalize stroke width too
            stroke.points.push_back(D2D1::Point2F(normX, normY));
            m_markupStrokes.push_back(stroke);
            UpdateRendererMarkup();
            SetCapture(m_window->GetHwnd());
        }
    } else if (m_editMode == EditMode::Text) {
        // Convert screen coords to normalized image coords
        D2D1_RECT_F imageRect = m_renderer->GetScreenImageRect();
        float imageW = imageRect.right - imageRect.left;
        float imageH = imageRect.bottom - imageRect.top;
        if (imageW > 0 && imageH > 0) {
            // Start inline text editing at click position
            m_isEditingText = true;
            m_editingText.clear();
            m_editingTextX = (static_cast<float>(x) - imageRect.left) / imageW;
            m_editingTextY = (static_cast<float>(y) - imageRect.top) / imageH;
            UpdateRendererText();
            InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        }
    } else if (m_editMode == EditMode::Erase) {
        // Start erasing - will continue to erase as mouse drags
        PushUndoState();
        m_isErasing = true;
        SetCapture(m_window->GetHwnd());
        // Erase at initial click position
        EraseAtPoint(x, y);
    } else {
        m_isPanning = true;
        m_lastMouseX = x;
        m_lastMouseY = y;
        SetCapture(m_window->GetHwnd());
    }
}

void App::OnMouseUp(int x, int y) {
    (void)x; (void)y;
    if (m_isCropDragging) {
        m_isCropDragging = false;
        ReleaseCapture();
    } else if (m_isDrawing) {
        m_isDrawing = false;
        ReleaseCapture();
    } else if (m_isErasing) {
        m_isErasing = false;
        ReleaseCapture();
    } else {
        m_isPanning = false;
        ReleaseCapture();
    }
}

void App::OnMouseMove(int x, int y) {
    if (m_isCropDragging) {
        m_cropEndX = x;
        m_cropEndY = y;
        // Update crop rect in renderer
        float left = static_cast<float>(std::min(m_cropStartX, m_cropEndX));
        float top = static_cast<float>(std::min(m_cropStartY, m_cropEndY));
        float right = static_cast<float>(std::max(m_cropStartX, m_cropEndX));
        float bottom = static_cast<float>(std::max(m_cropStartY, m_cropEndY));
        m_renderer->SetCropRect(D2D1::RectF(left, top, right, bottom));
        InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
    } else if (m_isDrawing && !m_markupStrokes.empty()) {
        // Convert screen coords to normalized image coords
        D2D1_RECT_F imageRect = m_renderer->GetScreenImageRect();
        float imageW = imageRect.right - imageRect.left;
        float imageH = imageRect.bottom - imageRect.top;
        if (imageW > 0 && imageH > 0) {
            float normX = (static_cast<float>(x) - imageRect.left) / imageW;
            float normY = (static_cast<float>(y) - imageRect.top) / imageH;
            m_markupStrokes.back().points.push_back(D2D1::Point2F(normX, normY));
            UpdateRendererMarkup();
            InvalidateRect(m_window->GetHwnd(), nullptr, FALSE);
        }
    } else if (m_isErasing) {
        // Continue erasing as mouse drags
        EraseAtPoint(x, y);
    } else if (m_isPanning) {
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
