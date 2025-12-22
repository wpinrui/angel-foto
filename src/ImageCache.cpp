#include "pch.h"
#include "ImageCache.h"

ImageCache::ImageCache() {}

ImageCache::~ImageCache() {
    Shutdown();
}

void ImageCache::Initialize(ImageLoader* loader) {
    m_loader = loader;
    m_running = true;
    m_workerThread = std::thread(&ImageCache::WorkerThread, this);
}

void ImageCache::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    Clear();
}

std::shared_ptr<ImageData> ImageCache::Get(const std::wstring& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_cache.find(filePath);
    if (it != m_cache.end()) {
        // Move to end of access order (most recently used)
        auto orderIt = std::find(m_accessOrder.begin(), m_accessOrder.end(), filePath);
        if (orderIt != m_accessOrder.end()) {
            m_accessOrder.erase(orderIt);
            m_accessOrder.push_back(filePath);
        }
        return it->second;
    }

    return nullptr;
}

void ImageCache::Prefetch(const std::vector<std::wstring>& filePaths) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& path : filePaths) {
        // Skip if already cached or queued
        if (m_cache.find(path) != m_cache.end()) {
            continue;
        }

        // Check if already in queue
        bool inQueue = false;
        std::queue<std::wstring> tempQueue = m_loadQueue;
        while (!tempQueue.empty()) {
            if (tempQueue.front() == path) {
                inQueue = true;
                break;
            }
            tempQueue.pop();
        }

        if (!inQueue) {
            m_loadQueue.push(path);
        }
    }

    m_cv.notify_one();
}

void ImageCache::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
    m_accessOrder.clear();

    // Clear queue
    std::queue<std::wstring> empty;
    std::swap(m_loadQueue, empty);
}

void ImageCache::WorkerThread() {
    // Note: This is a simplified implementation
    // For true async D2D bitmap creation, we'd need to:
    // 1. Decode image to CPU memory on worker thread
    // 2. Send message to main thread to create D2D bitmap
    // For now, we load synchronously but on demand

    while (m_running) {
        std::wstring pathToLoad;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_running || !m_loadQueue.empty();
            });

            if (!m_running) {
                break;
            }

            if (!m_loadQueue.empty()) {
                pathToLoad = m_loadQueue.front();
                m_loadQueue.pop();
            }
        }

        if (!pathToLoad.empty() && m_loader) {
            // Load the image
            // Note: In a full implementation, this would need thread synchronization
            // with D2D resources. For now, we'll skip background loading
            // and rely on fast synchronous loading instead.

            // The proper way would be to:
            // 1. Use WIC to decode to IWICBitmap on this thread
            // 2. Post message to main thread to convert to ID2D1Bitmap
        }
    }
}

void ImageCache::ProcessQueue() {
    // This would be called from the main thread to process loaded images
    // and create D2D bitmaps from WIC bitmaps decoded on the worker thread
}
