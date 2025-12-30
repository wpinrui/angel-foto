#pragma once
#include "pch.h"

class FolderNavigator {
public:
    FolderNavigator() = default;
    ~FolderNavigator() = default;

    // Set current file and scan folder for images
    void SetCurrentFile(const std::wstring& filePath);

    // Navigation
    bool GoToNext();
    bool GoToPrevious();
    bool GoToFirst();
    bool GoToLast();
    bool GoToIndex(size_t index);

    // Current state
    std::wstring GetCurrentFilePath() const;
    size_t GetCurrentIndex() const { return m_currentIndex; }
    size_t GetTotalCount() const { return m_imageFiles.size(); }
    bool HasNext() const { return m_currentIndex + 1 < m_imageFiles.size(); }
    bool HasPrevious() const { return m_currentIndex > 0; }

    // Get adjacent file paths for pre-loading
    std::vector<std::wstring> GetAdjacentFiles(size_t count = 3) const;

    // File operations
    bool DeleteCurrentFile();  // Moves to recycle bin
    bool RenameCurrentFile(const std::wstring& newName);

    // Refresh file list (after external changes)
    void Refresh();

    // Clear all files
    void Clear();

private:
    void ScanFolder(const std::wstring& folderPath);

    std::vector<std::wstring> m_imageFiles;
    size_t m_currentIndex = 0;
    std::wstring m_currentFolder;
};
