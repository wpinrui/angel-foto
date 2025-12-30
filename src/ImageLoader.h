#pragma once
#include "pch.h"

struct ImageData {
    ComPtr<ID2D1Bitmap> bitmap;
    std::wstring filePath;
    int width = 0;
    int height = 0;

    // For animated GIF
    bool isAnimated = false;
    std::vector<ComPtr<ID2D1Bitmap>> frames;
    std::vector<UINT> frameDelays; // in milliseconds
    UINT currentFrame = 0;
};

class ImageLoader {
public:
    ImageLoader() = default;
    ~ImageLoader() = default;

    void Initialize(ID2D1DeviceContext* deviceContext, IWICImagingFactory* wicFactory);

    // Load image from file path (synchronous)
    std::shared_ptr<ImageData> LoadImage(const std::wstring& filePath);

    // Load image asynchronously
    void LoadImageAsync(const std::wstring& filePath,
        std::function<void(std::shared_ptr<ImageData>)> callback);

    // Check if file is a supported image format
    static bool IsSupportedFormat(const std::wstring& filePath);

private:
    ComPtr<ID2D1Bitmap> LoadBitmapFromFile(const std::wstring& filePath);
    std::shared_ptr<ImageData> LoadAnimatedGif(const std::wstring& filePath);

    ID2D1DeviceContext* m_deviceContext = nullptr;
    IWICImagingFactory* m_wicFactory = nullptr;

    static const std::vector<std::wstring> s_supportedExtensions;

    // GIF animation constants
    static constexpr UINT DEFAULT_FRAME_DELAY_MS = 100;
    static constexpr UINT MIN_FRAME_DELAY_MS = 20;
    static constexpr UINT CENTISECONDS_TO_MS = 10;
};
