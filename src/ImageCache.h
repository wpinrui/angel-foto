#pragma once
#include "pch.h"
#include "ImageLoader.h"

class ImageCache {
public:
    ImageCache();
    ~ImageCache();

    void Initialize(ImageLoader* loader);
    void Shutdown();

    // Get cached image (returns nullptr if not cached)
    std::shared_ptr<ImageData> Get(const std::wstring& filePath);

    // Request background loading of files
    void Prefetch(const std::vector<std::wstring>& filePaths);

    // Clear cache
    void Clear();

    // Set maximum cache size (number of images)
    void SetMaxSize(size_t maxSize) { m_maxSize = maxSize; }

private:
    void WorkerThread();
    void ProcessQueue();

    ImageLoader* m_loader = nullptr;

    // Cache storage (LRU-style)
    std::unordered_map<std::wstring, std::shared_ptr<ImageData>> m_cache;
    std::vector<std::wstring> m_accessOrder; // Most recent at back
    size_t m_maxSize = 10;

    // Background loading
    std::thread m_workerThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::wstring> m_loadQueue;
    std::atomic<bool> m_running{ false };
};
