#include "pch.h"
#include "ImageLoader.h"

const std::vector<std::wstring> ImageLoader::s_supportedExtensions = {
    L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tiff", L".tif",
    L".webp", L".heic", L".heif", L".ico", L".jfif"
};

ImageLoader::ImageLoader() {}

void ImageLoader::Initialize(ID2D1DeviceContext* deviceContext, IWICImagingFactory* wicFactory) {
    m_deviceContext = deviceContext;
    m_wicFactory = wicFactory;
}

bool ImageLoader::IsSupportedFormat(const std::wstring& filePath) {
    fs::path path(filePath);
    std::wstring ext = path.extension().wstring();

    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    return std::find(s_supportedExtensions.begin(), s_supportedExtensions.end(), ext)
        != s_supportedExtensions.end();
}

std::shared_ptr<ImageData> ImageLoader::LoadImage(const std::wstring& filePath) {
    if (!m_deviceContext || !m_wicFactory) {
        return nullptr;
    }

    fs::path path(filePath);
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    // Check for animated GIF
    if (ext == L".gif") {
        auto gifData = LoadAnimatedGif(filePath);
        if (gifData && gifData->isAnimated) {
            return gifData;
        }
    }

    // Load as static image
    auto bitmap = LoadBitmapFromFile(filePath);
    if (!bitmap) {
        return nullptr;
    }

    auto imageData = std::make_shared<ImageData>();
    imageData->bitmap = bitmap;
    imageData->filePath = filePath;
    auto size = bitmap->GetSize();
    imageData->width = static_cast<int>(size.width);
    imageData->height = static_cast<int>(size.height);
    imageData->isAnimated = false;

    return imageData;
}

void ImageLoader::LoadImageAsync(const std::wstring& filePath,
    std::function<void(std::shared_ptr<ImageData>)> callback) {
    // Note: For true async loading, we'd need to handle D2D resources on the main thread
    // For now, this is a placeholder that loads synchronously
    // A proper implementation would decode on background thread, then create D2D bitmap on main thread
    auto result = LoadImage(filePath);
    if (callback) {
        callback(result);
    }
}

ComPtr<ID2D1Bitmap> ImageLoader::LoadBitmapFromFile(const std::wstring& filePath) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = m_wicFactory->CreateDecoderFromFilename(
        filePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder
    );
    if (FAILED(hr)) return nullptr;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return nullptr;

    ComPtr<IWICFormatConverter> converter;
    hr = m_wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return nullptr;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeMedianCut
    );
    if (FAILED(hr)) return nullptr;

    ComPtr<ID2D1Bitmap> bitmap;
    hr = m_deviceContext->CreateBitmapFromWicBitmap(
        converter.Get(),
        nullptr,
        &bitmap
    );
    if (FAILED(hr)) return nullptr;

    return bitmap;
}

std::shared_ptr<ImageData> ImageLoader::LoadAnimatedGif(const std::wstring& filePath) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = m_wicFactory->CreateDecoderFromFilename(
        filePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder
    );
    if (FAILED(hr)) return nullptr;

    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || frameCount == 0) return nullptr;

    auto imageData = std::make_shared<ImageData>();
    imageData->filePath = filePath;
    imageData->isAnimated = (frameCount > 1);

    // Get global metadata for canvas size
    UINT canvasWidth = 0, canvasHeight = 0;

    ComPtr<IWICMetadataQueryReader> globalMetadata;
    if (SUCCEEDED(decoder->GetMetadataQueryReader(&globalMetadata))) {
        PROPVARIANT propValue;
        PropVariantInit(&propValue);

        if (SUCCEEDED(globalMetadata->GetMetadataByName(L"/logscrdesc/Width", &propValue))) {
            canvasWidth = propValue.uiVal;
            PropVariantClear(&propValue);
        }
        if (SUCCEEDED(globalMetadata->GetMetadataByName(L"/logscrdesc/Height", &propValue))) {
            canvasHeight = propValue.uiVal;
            PropVariantClear(&propValue);
        }
    }

    // Create a canvas bitmap for compositing frames
    ComPtr<IWICBitmap> canvas;
    if (canvasWidth > 0 && canvasHeight > 0) {
        m_wicFactory->CreateBitmap(canvasWidth, canvasHeight,
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &canvas);
    }

    for (UINT i = 0; i < frameCount; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(i, &frame);
        if (FAILED(hr)) continue;

        // Get frame delay
        UINT delay = DEFAULT_FRAME_DELAY_MS;
        ComPtr<IWICMetadataQueryReader> frameMetadata;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&frameMetadata))) {
            PROPVARIANT propValue;
            PropVariantInit(&propValue);
            if (SUCCEEDED(frameMetadata->GetMetadataByName(L"/grctlext/Delay", &propValue))) {
                delay = propValue.uiVal * CENTISECONDS_TO_MS;
                if (delay < MIN_FRAME_DELAY_MS) delay = DEFAULT_FRAME_DELAY_MS;
                PropVariantClear(&propValue);
            }
        }
        imageData->frameDelays.push_back(delay);

        // Convert frame to BGRA
        ComPtr<IWICFormatConverter> converter;
        hr = m_wicFactory->CreateFormatConverter(&converter);
        if (FAILED(hr)) continue;

        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeMedianCut
        );
        if (FAILED(hr)) continue;

        ComPtr<ID2D1Bitmap> bitmap;
        hr = m_deviceContext->CreateBitmapFromWicBitmap(
            converter.Get(),
            nullptr,
            &bitmap
        );
        if (FAILED(hr)) continue;

        imageData->frames.push_back(bitmap);

        // Set first frame as current bitmap
        if (i == 0) {
            imageData->bitmap = bitmap;
            auto size = bitmap->GetSize();
            imageData->width = static_cast<int>(size.width);
            imageData->height = static_cast<int>(size.height);
        }
    }

    if (imageData->frames.empty()) {
        return nullptr;
    }

    return imageData;
}
