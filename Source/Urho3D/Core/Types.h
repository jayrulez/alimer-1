// Copyright © Amer Koleci and Contributors.
// Licensed under the MIT License (MIT). See LICENSE in the repository root for more information.

#pragma once

#ifdef URHO3D_IS_BUILDING
#include "PlatformDef.h"
#else
#include <Urho3D/PlatformDef.h>
#endif
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <memory>
#include <type_traits>

#define ALIMER_DISABLE_COPY(_Class) \
_Class(const _Class&) = delete; _Class& operator=(const _Class&) = delete; \
_Class(_Class&&) = default; _Class& operator=(_Class&&) = default;

#define ALIMER_DISABLE_MOVE(_Class) \
_Class(_Class&&) = delete; _Class& operator=(_Class&&) = delete;

#define ALIMER_DISABLE_COPY_MOVE(_Class) \
_Class(const _Class&) = delete; _Class& operator=(const _Class&) = delete; ALIMER_DISABLE_MOVE(_Class)

#define ALIMER_DEFINE_ENUM_BITWISE_OPERATORS(EnumType) \
inline constexpr EnumType operator | (EnumType a, EnumType b) { return EnumType(std::underlying_type<EnumType>::type(a) | std::underlying_type<EnumType>::type(b)); } \
inline constexpr EnumType& operator |= (EnumType &a, EnumType b) { return a = a | b; } \
inline constexpr EnumType operator & (EnumType a, EnumType b) { return EnumType(std::underlying_type<EnumType>::type(a) & std::underlying_type<EnumType>::type(b)); } \
inline constexpr EnumType& operator &= (EnumType &a, EnumType b) { return a = a & b; } \
inline constexpr EnumType operator ~ (EnumType a) { return EnumType(~std::underlying_type<EnumType>::type(a)); } \
inline constexpr EnumType operator ^ (EnumType a, EnumType b) { return EnumType(std::underlying_type<EnumType>::type(a) ^ std::underlying_type<EnumType>::type(b)); } \
inline constexpr EnumType& operator ^= (EnumType &a, EnumType b) { return a = a ^ b; } \
inline constexpr bool operator ==(EnumType a, std::underlying_type<EnumType>::type b) { return std::underlying_type<EnumType>::type(a) == b; } \
inline constexpr bool operator !=(EnumType a, std::underlying_type<EnumType>::type b) { return std::underlying_type<EnumType>::type(a) != b; }

namespace Urho3D
{
    /**
     * Hash for enum types, to be used instead of std::hash<T> when T is an enum.
     *
     * Until C++14, std::hash<T> is not defined if T is a enum (see
     * http://www.open-std.org/jtc1/sc22/wg21/docs/lwg-defects.html#2148).
     * But even with C++14, as of october 2016, std::hash for enums is not widely
     * implemented by compilers, so here when T is a enum, we use EnumClassHash
     * instead of std::hash. (For instance, in Alimer::HashCombine(), or
     * Alimer::UnorderedMap.)
     */
    struct EnumClassHash
    {
        template <typename T>
        constexpr std::size_t operator()(T t) const
        {
            return static_cast<std::size_t>(t);
        }
    };

    /** Hasher that handles custom enums automatically. */
    template <typename Key>
    using HashType = typename std::conditional<std::is_enum<Key>::value, EnumClassHash, std::hash<Key>>::type;

    /** Generates a hash for the provided type. Type must have a std::hash specialization. */
    template <class T>
    size_t Hash(const T& v)
    {
        using HashType = typename std::conditional<std::is_enum<T>::value, EnumClassHash, std::hash<T>>::type;

        HashType hasher;
        return hasher(v);
    }

    // Source: https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
    template <typename T>
    inline void HashCombine(std::size_t& seed, const T& v)
    {
        HashType<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    template <typename T, typename... Rest>
    inline void HashCombine(size_t& seed, const T& v, Rest&&... rest)
    {
        HashType<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        HashCombine(seed, std::forward<Rest>(rest)...);
    }

    constexpr size_t ComputeStringHash(const char* input)
    {
        // https://stackoverflow.com/questions/2111667/compile-time-string-hashing
        size_t hash = sizeof(size_t) == 8 ? 0xcbf29ce484222325 : 0x811c9dc5;
        const size_t prime = sizeof(size_t) == 8 ? 0x00000100000001b3 : 0x01000193;

        while (*input)
        {
            hash ^= static_cast<size_t>(*input);
            hash *= prime;
            ++input;
        }

        return hash;
    }

    template <typename T>
    void SafeDelete(T*& resource)
    {
        delete resource;
        resource = nullptr;
    }
}

namespace std
{
    template <typename U, typename V> struct hash<std::pair<U, V>>
    {
        std::size_t operator()(std::pair<U, V> const& value) const noexcept
        {
            std::size_t h = 0;
            Urho3D::HashCombine(h, value.first);
            Urho3D::HashCombine(h, value.second);
            return h;
        }
    };
}
