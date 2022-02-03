// Copyright © Amer Koleci and Contributors.
// Licensed under the MIT License (MIT). See LICENSE in the repository root for more information.

#pragma once

#include "PlatformDef.h"

#ifdef __ANDROID__
#   include <android/log.h>
#elif TARGET_OS_IOS || TARGET_OS_TV
#   include <sys/syslog.h>
#elif TARGET_OS_MAC || defined(__linux__)
#   include <unistd.h>
#elif defined(_WIN32)
#   define NOMINMAX
#   define NODRAWTEXT
#   define NOGDI
#   define NOBITMAP
#   define NOMCX
#   define NOSERVICE
#   define NOHELP
#   define WIN32_LEAN_AND_MEAN
#   include <Windows.h>
#   include <memory>
#   include <stdexcept>

namespace Urho3D
{
    // Helper class for COM exceptions
    class COMException : public std::exception
    {
    public:
        COMException(HRESULT hr) noexcept : result(hr) {}

        const char* what() const noexcept override
        {
            static char s_str[64] = {};
            sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
            return s_str;
        }

    private:
        HRESULT result;
    };

    // Helper utility converts D3D API failures into exceptions.
    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw COMException(hr);
        }
    }

    inline std::string GetWin32ErrorString(DWORD errorCode)
    {
        char errorString[MAX_PATH];
        ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM,
            0,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            errorString,
            MAX_PATH,
            NULL);

        return std::string(errorString);
    }

    inline std::wstring GetWin32ErrorStringWide(DWORD errorCode)
    {
        WCHAR errorString[MAX_PATH];
        ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,
            0,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            errorString,
            MAX_PATH,
            NULL);

        return std::wstring(errorString);
    }

    template <typename T>
    void SafeRelease(T& resource)
    {
        if (resource)
        {
            resource->Release();
            resource = nullptr;
        }
    }
}
#elif defined(__EMSCRIPTEN__)
#   include <emscripten.h>
#   include <emscripten/html5.h>
#endif
