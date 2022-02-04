//
// Copyright (c) 2008-2022 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "../Container/Str.h"

namespace Urho3D
{
    class StringHashRegister;

    /// 32-bit hash value for a string.
    class URHO3D_API StringHash
    {
    public:
        /// Construct with zero value.
        StringHash() noexcept
            : value(0)
        {
        }

        /// Copy-construct from another hash.
        StringHash(const StringHash& rhs) noexcept = default;

        /// Construct with an initial value.
        explicit StringHash(uint32_t value_) noexcept
            : value(value_)
        {
        }

        /// Construct from a C string.
        StringHash(const char* str) noexcept;        // NOLINT(google-explicit-constructor)
        /// Construct from a string.
        StringHash(const String& str) noexcept;      // NOLINT(google-explicit-constructor)
        /// Construct from a string.
        StringHash(const std::string& str) noexcept;      // NOLINT(google-explicit-constructor)
        /// Construct from a string.
        StringHash( std::string_view str) noexcept;      // NOLINT(google-explicit-constructor)

        /// Assign from another hash.
        StringHash& operator =(const StringHash& rhs) noexcept = default;

        /// Add a hash.
        StringHash operator +(const StringHash& rhs) const
        {
            StringHash ret;
            ret.value = value + rhs.value;
            return ret;
        }

        /// Add-assign a hash.
        StringHash& operator +=(const StringHash& rhs)
        {
            value += rhs.value;
            return *this;
        }

        /// Test for equality with another hash.
        bool operator ==(const StringHash& rhs) const { return value == rhs.value; }

        /// Test for inequality with another hash.
        bool operator !=(const StringHash& rhs) const { return value != rhs.value; }

        /// Test if less than another hash.
        bool operator <(const StringHash& rhs) const { return value < rhs.value; }

        /// Test if greater than another hash.
        bool operator >(const StringHash& rhs) const { return value > rhs.value; }

        /// Return true if nonzero hash value.
        explicit operator bool() const { return value != 0; }

        /// Return hash value.
        /// @property
        uint32_t Value() const { return value; }

        /// Return as string.
        String ToString() const;

        /// Return string which has specific hash value. Return first string if many (in order of calculation). Use for debug purposes only. Return empty string if URHO3D_HASH_DEBUG is off.
        String Reverse() const;

        /// Return hash value for HashMap.
        uint32_t ToHash() const { return value; }

        /// Calculate hash value from a C string.
        static uint32_t Calculate(const char* str, uint32_t hash = 0);

        /// Get global StringHashRegister. Use for debug purposes only. Return nullptr if URHO3D_HASH_DEBUG is off.
        static StringHashRegister* GetGlobalStringHashRegister();

        /// Zero hash.
        static const StringHash ZERO;

    private:
        /// Hash value.
        uint32_t value{ 0u };
    };

    static_assert(sizeof(StringHash) == sizeof(uint32_t), "Unexpected StringHash size.");
}

namespace std
{
    template<> struct hash<Urho3D::StringHash>
    {
        std::size_t operator()(const Urho3D::StringHash& value) const noexcept
        {
            return value.Value();
        }
    };
}
