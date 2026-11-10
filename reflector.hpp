/*
Reflector - a lightweight C++ reflection library


# Usage

To run test suite, define REFLECTOR_ENABLE_TESTS macro and call reflector::run_test_suite().


# License

Copyright (c) 2026 Ryan de Barros

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef REFLECTOR_INCLUDED
#define REFLECTOR_INCLUDED

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace reflector
{

// ================================ PUBLIC API ================================

template<typename ty>
constexpr std::string_view compiler_type_name() noexcept;

struct template_node;

template<typename ty>
const template_node& compiler_type_tree() noexcept;

struct type_erasure;

template<typename ty>
constexpr type_erasure erase_type() noexcept;

template<typename ty, typename U>
constexpr bool erases_to() noexcept;

template<typename ty>
constexpr const ty* resolve_type(type_erasure erasure, const void* ptr) noexcept;

template<typename ty>
constexpr ty* resolve_type(type_erasure erasure, void* ptr) noexcept;

template<typename ty>
constexpr const ty* resolve_type(type_erasure erasure, const ty* ptr) noexcept;

template<typename ty>
constexpr ty* resolve_type(type_erasure erasure, ty* ptr) noexcept;

class box;

template<typename ty, typename... Args> requires (!std::is_void_v<ty>)
box make_box(Args&&... args);

template<typename ty, typename... Args> requires (!std::is_void_v<ty>)
void copy_box(box& b, Args&&... args);

template<typename ty>
box forward_to_box(ty&& obj);

template<typename ty>
void forward_into_box(box& b, ty&& obj);

// ================================== DETAIL ==================================

namespace detail
{

constexpr std::string_view extract_type_name(std::string_view signature) noexcept
{
#if defined(_MSC_VER)
    constexpr std::string_view prefix = "raw_compiler_type_name<";
#else
#pragma error("Compiler not supported")
#endif

    const auto begin = signature.find(prefix);
    if (begin == std::string_view::npos)
        return {};

    const auto start = begin + prefix.size();
    const auto end = signature.find(">(", start);
    if (end == std::string_view::npos)
        return {};
    
    return signature.substr(start, end - start);
}

template<typename ty>
constexpr std::string_view raw_compiler_type_name()
{
#if defined(_MSC_VER)
    return extract_type_name(__FUNCSIG__);
#else
#pragma error("Compiler not supported")
#endif
}

constexpr bool is_identifier_char(char c) noexcept
{
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9')
        || c == '_';
}

constexpr bool is_elaborated_keyword(std::string_view token) noexcept
{
    return token == "class"
        || token == "struct"
        || token == "enum"
        || token == "union";
}

template<size_t N>
struct static_string
{
    char data[N]{};
    size_t size = 0;

    constexpr std::string_view view() const noexcept
    {
        return { data, size };
    }
};

template<size_t N>
constexpr static_string<N> normalize_type_name(std::string_view input) noexcept
{
    static_string<N> result{};

    for (size_t i = 0; i < input.size();)
    {
        if (!is_identifier_char(input[i]))
        {
            result.data[result.size++] = input[i++];
            continue;
        }

        const auto start = i;

        while (i < input.size() && is_identifier_char(input[i]))
            ++i;

        const auto token = input.substr(start, i - start);
        if (is_elaborated_keyword(token))
            continue;

        for (const auto c : token)
            result.data[result.size++] = c;
    }

    return result;
}

template<typename ty>
constexpr auto compiler_type_name_storage = normalize_type_name<raw_compiler_type_name<ty>().size()>(
    raw_compiler_type_name<ty>());

template<typename ty>
constexpr std::string_view compiler_type_name_impl() noexcept
{
    return compiler_type_name_storage<ty>.view();
}

} // detail

template<typename ty>
constexpr std::string_view compiler_type_name() noexcept
{
    return detail::compiler_type_name_impl<ty>();
}

struct template_node
{
    std::string name;
    std::vector<template_node> parameters;

    constexpr template_node() = default;

    constexpr bool is_template() const noexcept
    {
        return !parameters.empty();
    }
};

namespace detail
{

constexpr bool is_whitespace(char c)
{
    return c == ' '
        || c == '\t'
        || c == '\n'
        || c == '\r';
}

constexpr std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && is_whitespace(value.front()))
        value.remove_prefix(1);

    while (!value.empty() && is_whitespace(value.back()))
        value.remove_suffix(1);

    return value;
}

constexpr size_t find_matching_template_end(std::string_view input, size_t opening) noexcept
{
    size_t depth = 0;

    for (size_t i = opening; i < input.size(); ++i)
    {
        if (input[i] == '<')
            ++depth;
        else if (input[i] == '>' && (--depth == 0))
            return i;
    }

    return std::string_view::npos;
}

constexpr template_node parse_template_tree(std::string_view input) noexcept
{
    input = trim(input);

    template_node result;

    const auto opening = input.find('<');

    if (opening == std::string_view::npos)
    {
        result.name = input;
        return result;
    }

    const auto closing = find_matching_template_end(input, opening);

    if (closing == std::string_view::npos)
    {
        result.name = input;
        return result;
    }

    result.name = trim(input.substr(0, opening));

    const auto arguments = input.substr(opening + 1, closing - opening - 1);

    size_t start = 0;
    size_t depth = 0;

    for (size_t i = 0; i < arguments.size(); ++i)
    {
        if (arguments[i] == '<')
            ++depth;
        else if (arguments[i] == '>')
            --depth;
        else if (arguments[i] == ',' && depth == 0)
        {
            result.parameters.push_back(parse_template_tree(arguments.substr(start, i - start)));
            start = i + 1;
        }
    }

    if (start < arguments.size())
        result.parameters.push_back(parse_template_tree(arguments.substr(start)));

    return result;
}

template<typename ty>
inline const auto compiler_type_tree_storage = parse_template_tree(compiler_type_name<ty>());

} // detail

template<typename ty>
const template_node& compiler_type_tree() noexcept
{
    return detail::compiler_type_tree_storage<ty>;
}

namespace detail
{

constexpr std::uint64_t fnv1a_hash(std::string_view str) noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (char c : str)
    {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x00000100000001B3ULL;
    }
    return hash;
}

} // detail

struct type_erasure
{
    template<typename ty>
    friend constexpr type_erasure erase_type() noexcept;

    struct descriptor
    {
        friend class box;
        friend class type_erasure;

    public:
        std::string_view name;
        const template_node& (*template_tree)();
        size_t type_hash;

    private:
        void (*dtor_op_)(void*);
        void* (*copy_op_)(const void*);
        void (*copy_assign_op_)(void*, const void*);
        void (*move_assign_op_)(void*, void*);
        size_t(*hasher_op_)(const void*);
        bool (*equal_op_)(const void*, const void*);
        bool (*not_equal_op_)(const void*, const void*);

    private:
        constexpr descriptor(
            std::string_view name,
            const template_node& (*template_tree)(),
            size_t type_hash,
            void (*dtor_op)(void*),
            void* (*copy_op)(const void*),
            void (*copy_assign_op)(void*, const void*),
            void (*move_assign_op)(void*, void*),
            size_t(*hasher_op)(const void*),
            bool (*equal_op)(const void*, const void*),
            bool (*not_equal_op)(const void*, const void*)
        ) noexcept
            : name(name)
            , template_tree(template_tree)
            , type_hash(type_hash)
            , dtor_op_(dtor_op)
            , copy_op_(copy_op)
            , copy_assign_op_(copy_assign_op)
            , move_assign_op_(move_assign_op)
            , hasher_op_(hasher_op)
            , equal_op_(equal_op)
            , not_equal_op_(not_equal_op)
        {
        }
    };

private:
    template<typename ty>
    static constexpr descriptor type_desc = descriptor(
        compiler_type_name<ty>(),
        []() -> const template_node& {
            return compiler_type_tree<ty>();
        },
        detail::fnv1a_hash(compiler_type_name<ty>()),
        []() {
            if constexpr (!std::is_void_v<ty>)
            {
                return +[](void* ptr) {
                    delete static_cast<ty*>(ptr);
                };
            }
            else
                return nullptr;
        }(),
        []() {
            if constexpr (!std::is_void_v<ty> && std::is_copy_constructible_v<ty>)
            {
                return +[](const void* ptr) -> void* {
                    return ptr ? new ty(*static_cast<const ty*>(ptr)) : nullptr;
                };
            }
            else
                return nullptr;
        }(),
        []() {
            if constexpr (!std::is_void_v<ty> && std::is_copy_assignable_v<ty>)
            {
                return +[](void* to, const void* from) {
                    *static_cast<ty*>(to) = *static_cast<const ty*>(from);
                };
            }
            else
                return nullptr;
        }(),
        []() {
            if constexpr (!std::is_void_v<ty> && std::is_move_assignable_v<ty>)
            {
                return +[](void* to, void* from) {
                    *static_cast<ty*>(to) = std::move(*static_cast<ty*>(from));
                };
            }
            else
                return nullptr;
        }(),
        []() {
            if constexpr (!std::is_void_v<ty> && requires(const ty & value)
            {
                {
                    std::hash<ty>{}(value)
                } -> std::convertible_to<size_t>;
            })
            {
                return +[](const void* ptr) {
                    return std::hash<ty>{}(*static_cast<const ty*>(ptr));
                };
            }
            else
                return nullptr;
        }(),
        []() {
            if constexpr (!std::is_void_v<ty> && requires(const ty & a, const ty & b)
            {
                {
                    a == b
                } -> std::convertible_to<bool>;
            })
            {
                return +[](const void* lhs, const void* rhs) -> bool {
                    return *static_cast<const ty*>(lhs) == *static_cast<const ty*>(rhs);
                };
            }
            else
                return nullptr;
        }(),
        []() {
            if constexpr (!std::is_void_v<ty> && requires(const ty & a, const ty & b)
            {
                {
                    a != b
                } -> std::convertible_to<bool>;
            })
            {
                return +[](const void* lhs, const void* rhs) -> bool {
                    return *static_cast<const ty*>(lhs) != *static_cast<const ty*>(rhs);
                };
            }
            else
                return nullptr;
        }()
        );

    const descriptor* desc_ = nullptr;

    constexpr explicit type_erasure(const descriptor* desc) noexcept
        : desc_(desc)
    {
    }

public:
    constexpr type_erasure() noexcept
        : desc_(&type_desc<void>)
    {
    }

    constexpr bool operator==(const type_erasure& o) const noexcept
    {
        return desc_ == o.desc_;
    }

    constexpr bool operator!=(const type_erasure& o) const noexcept
    {
        return desc_ != o.desc_;
    }

    [[nodiscard]]
    constexpr size_t hash() const noexcept
    {
        return desc_->type_hash;
    }

    [[nodiscard]]
    constexpr std::string_view name() const noexcept
    {
        return desc_->name;
    }

    [[nodiscard]]
    constexpr const descriptor& desc() const noexcept
    {
        return *desc_;
    }
};

template<typename ty>
constexpr type_erasure erase_type() noexcept
{
    return type_erasure(&type_erasure::type_desc<ty>);
}

template<typename ty, typename U>
constexpr bool erases_to() noexcept
{
    return erase_type<ty>() == erase_type<U>();
}

template<typename ty>
constexpr const ty* resolve_type(type_erasure erasure, const void* ptr) noexcept
{
    return erase_type<ty>() == erasure ? static_cast<const ty*>(ptr) : nullptr;
}

template<typename ty>
constexpr ty* resolve_type(type_erasure erasure, void* ptr) noexcept
{
    return erase_type<ty>() == erasure ? static_cast<ty*>(ptr) : nullptr;
}

template<typename ty>
constexpr const ty* resolve_type(type_erasure erasure, const ty* ptr) noexcept
{
    return resolve_type<ty>(erasure, static_cast<const void*>(ptr));
}

template<typename ty>
constexpr ty* resolve_type(type_erasure erasure, ty* ptr) noexcept
{
    return resolve_type<ty>(erasure, static_cast<void*>(ptr));
}

class box
{
    void* raw_ = nullptr;
    type_erasure type_;

public:
    box() = default;

    template<typename ty>
    box(ty* raw)
        : raw_(raw)
        , type_(erase_type<ty>())
    {
    }

    box(const box& o)
        : raw_(o.try_copy(o.raw_))
        , type_(o.type_)
    {
        if (o.raw_ && !raw_)
            throw std::logic_error("Attempted to copy a non-copyable type");
    }

    box(box&& o) noexcept
        : raw_(o.raw_)
        , type_(o.type_)
    {
        o.raw_ = nullptr;
        o.type_ = {};
    }

    ~box()
    {
        try_dtor(raw_);
    }

    box& operator=(const box& o)
    {
        if (this != &o)
        {
            if (type_ != o.type_ || !try_copy_assign(raw_, o.raw_))
            {
                try_dtor(raw_);
                raw_ = o.try_copy(o.raw_);
            }

            type_ = o.type_;

            if (o.raw_ && !raw_)
                throw std::logic_error("Attempted to copy a non-copyable type");
        }

        return *this;
    }

    box& operator=(box&& o) noexcept
    {
        if (this != &o)
        {
            if (type_ != o.type_ || !try_move_assign(raw_, o.raw_))
            {
                try_dtor(raw_);
                raw_ = o.raw_;
            }

            type_ = o.type_;

            o.raw_ = nullptr;
            o.type_ = {};
        }

        return *this;
    }

    bool operator==(const box& o) const
    {
        if (raw_ == nullptr || o.raw_ == nullptr)
            return raw_ == o.raw_;

        if (type_ != o.type_)
            return false;

        if (auto* equal_op = type_.desc().equal_op_)
            return (*equal_op)(raw_, o.raw_);
        else
            throw std::runtime_error("Equality not defined for this type");
    }

    bool operator!=(const box& o) const
    {
        if (raw_ == nullptr || o.raw_ == nullptr)
            return raw_ != o.raw_;

        if (type_ != o.type_)
            return true;

        if (auto* not_equal_op = type_.desc().not_equal_op_)
            return (*not_equal_op)(raw_, o.raw_);
        else
            throw std::runtime_error("Inequality not defined for this type");
    }

public:
    template<typename ty>
    bool holds() const
    {
        return type_ == erase_type<ty>();
    }

    bool holds(type_erasure type) const
    {
        return type_ == type;
    }

    type_erasure type() const
    {
        return type_;
    }

    const void* unsafe_raw() const
    {
        return raw_;
    }

    void* unsafe_raw()
    {
        return raw_;
    }

    template<typename ty>
    const ty* as() const
    {
        if (holds<ty>())
            return static_cast<const ty*>(raw_);
        else
            return nullptr;
    }

    template<typename ty>
    ty* as()
    {
        if (holds<ty>())
            return static_cast<ty*>(raw_);
        else
            return nullptr;
    }

    template<typename ty>
    std::unique_ptr<ty> consume()
    {
        if (holds<ty>())
        {
            std::unique_ptr<ty> ptr(static_cast<ty*>(raw_));
            raw_ = nullptr;
            return ptr;
        }
        else
            return nullptr;
    }

    size_t hash() const
    {
        if (auto* hasher = type_.desc().hasher_op_)
            return (*hasher)(raw_);
        else
            throw std::runtime_error("Hashing not defined for this type");
    }

private:
    void try_dtor(void* ptr) const
    {
        if (auto* dtor = type_.desc().dtor_op_)
            (*dtor)(ptr);
    }

    void* try_copy(const void* ptr) const
    {
        if (auto* copy = type_.desc().copy_op_)
            return (*copy)(ptr);
        else
            return nullptr;
    }

    bool try_copy_assign(void* to, const void* from)
    {
        if (to && from)
        {
            if (auto* copy_assign = type_.desc().copy_assign_op_)
            {
                (*copy_assign)(to, from);
                return true;
            }
        }

        return false;
    }

    bool try_move_assign(void* to, void* from)
    {
        if (to && from)
        {
            if (auto* move_assign = type_.desc().move_assign_op_)
            {
                (*move_assign)(to, from);
                return true;
            }
        }

        return false;
    }
};

template<typename ty, typename... Args> requires (!std::is_void_v<ty>)
box make_box(Args&&... args)
{
    return box(new ty(std::forward<Args>(args)...));
}

template<typename ty, typename... Args> requires (!std::is_void_v<ty>)
void copy_box(box& b, Args&&... args)
{
    if (ty* obj = b.as<ty>())
        *obj = ty(std::forward<Args>(args)...);
    else
        b = make_box<ty>(std::forward<Args>(args)...);
}

template<typename ty>
box forward_to_box(ty&& obj)
{
    return make_box<std::remove_cvref_t<ty>>(std::forward<ty>(obj));
}

template<typename ty>
void forward_into_box(box& b, ty&& obj)
{
    copy_box<std::remove_cvref_t<ty>>(b, std::forward<ty>(obj));
}

} // reflector

template<>
struct std::hash<reflector::type_erasure>
{
    size_t operator()(reflector::type_erasure te) const
    {
        return te.hash();
    }
};

template<>
struct std::hash<reflector::box>
{
    size_t operator()(const reflector::box& box) const
    {
        return box.hash();
    }
};

#endif  // REFLECTOR_INCLUDED

// ================================ TEST SUITE ================================

#if defined(REFLECTOR_ENABLE_TESTS) && !defined(REFLECTOR_TESTS_INCLUDED)
#define REFLECTOR_TESTS_INCLUDED

namespace reflector
{

inline void run_test_suite()
{
    // TODO
}

} // reflector

#endif // REFLECTOR_TESTS_INCLUDED
