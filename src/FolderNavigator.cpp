#include "pch.h"
#include "FolderNavigator.h"
#include "ImageLoader.h"

FolderNavigator::FolderNavigator() {}

void FolderNavigator::SetCurrentFile(const std::wstring& filePath) {
    fs::path path(filePath);

    if (!fs::exists(path)) {
        return;
    }

    m_currentFolder = path.parent_path().wstring();
    ScanFolder(m_currentFolder);

    // Find current file in list
    std::wstring filename = path.filename().wstring();
    for (size_t i = 0; i < m_imageFiles.size(); ++i) {
        fs::path imgPath(m_imageFiles[i]);
        if (imgPath.filename().wstring() == filename) {
            m_currentIndex = i;
            break;
        }
    }
}

void FolderNavigator::ScanFolder(const std::wstring& folderPath) {
    m_imageFiles.clear();

    try {
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file()) {
                std::wstring path = entry.path().wstring();
                if (ImageLoader::IsSupportedFormat(path)) {
                    m_imageFiles.push_back(path);
                }
            }
        }

        // Sort alphabetically (case-insensitive)
        std::sort(m_imageFiles.begin(), m_imageFiles.end(),
            [](const std::wstring& a, const std::wstring& b) {
                return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });
    }
    catch (const std::exception&) {
        // Handle permission errors, etc.
    }
}

bool FolderNavigator::GoToNext() {
    if (m_currentIndex + 1 < m_imageFiles.size()) {
        m_currentIndex++;
        return true;
    }
    return false;
}

bool FolderNavigator::GoToPrevious() {
    if (m_currentIndex > 0) {
        m_currentIndex--;
        return true;
    }
    return false;
}

bool FolderNavigator::GoToFirst() {
    if (!m_imageFiles.empty() && m_currentIndex != 0) {
        m_currentIndex = 0;
        return true;
    }
    return false;
}

bool FolderNavigator::GoToLast() {
    if (!m_imageFiles.empty() && m_currentIndex != m_imageFiles.size() - 1) {
        m_currentIndex = m_imageFiles.size() - 1;
        return true;
    }
    return false;
}

bool FolderNavigator::GoToIndex(size_t index) {
    if (index < m_imageFiles.size()) {
        m_currentIndex = index;
        return true;
    }
    return false;
}

std::wstring FolderNavigator::GetCurrentFilePath() const {
    if (m_currentIndex < m_imageFiles.size()) {
        return m_imageFiles[m_currentIndex];
    }
    return L"";
}

std::vector<std::wstring> FolderNavigator::GetAdjacentFiles(size_t count) const {
    std::vector<std::wstring> result;

    if (m_imageFiles.empty()) {
        return result;
    }

    // Get files before current
    for (size_t i = 1; i <= count && m_currentIndex >= i; ++i) {
        result.push_back(m_imageFiles[m_currentIndex - i]);
    }

    // Get files after current
    for (size_t i = 1; i <= count && m_currentIndex + i < m_imageFiles.size(); ++i) {
        result.push_back(m_imageFiles[m_currentIndex + i]);
    }

    return result;
}

bool FolderNavigator::DeleteCurrentFile() {
    if (m_imageFiles.empty()) {
        return false;
    }

    std::wstring filePath = m_imageFiles[m_currentIndex];

    // Use Shell API to move to recycle bin
    SHFILEOPSTRUCTW fileOp = {};
    fileOp.wFunc = FO_DELETE;

    // Path must be double-null terminated
    std::wstring pathWithNull = filePath + L'\0';
    fileOp.pFrom = pathWithNull.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

    int result = SHFileOperationW(&fileOp);

    if (result == 0 && !fileOp.fAnyOperationsAborted) {
        // Remove from list
        m_imageFiles.erase(m_imageFiles.begin() + m_currentIndex);

        // Adjust index if needed
        if (m_currentIndex >= m_imageFiles.size() && m_currentIndex > 0) {
            m_currentIndex--;
        }

        return true;
    }

    return false;
}

bool FolderNavigator::RenameCurrentFile(const std::wstring& newName) {
    if (m_imageFiles.empty()) {
        return false;
    }

    fs::path currentPath(m_imageFiles[m_currentIndex]);
    fs::path newPath = currentPath.parent_path() / newName;

    try {
        fs::rename(currentPath, newPath);
        m_imageFiles[m_currentIndex] = newPath.wstring();
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

void FolderNavigator::Refresh() {
    if (m_currentFolder.empty()) {
        return;
    }

    std::wstring currentFile = GetCurrentFilePath();
    ScanFolder(m_currentFolder);

    // Try to find the same file again
    for (size_t i = 0; i < m_imageFiles.size(); ++i) {
        if (m_imageFiles[i] == currentFile) {
            m_currentIndex = i;
            return;
        }
    }

    // If not found, clamp index
    if (m_currentIndex >= m_imageFiles.size() && !m_imageFiles.empty()) {
        m_currentIndex = m_imageFiles.size() - 1;
    }
}
