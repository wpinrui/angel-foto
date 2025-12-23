#pragma once

// Windows headers
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>

// DXGI and Direct3D
#include <dxgi1_2.h>
#include <d3d11.h>

// Direct2D and DirectWrite
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>

// COM smart pointers
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// C++ Standard Library
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <cfloat>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <functional>
#include <optional>

namespace fs = std::filesystem;

// Helper macro for COM error checking
#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hr) if (FAILED(hr)) throw std::runtime_error("COM operation failed")
#endif
