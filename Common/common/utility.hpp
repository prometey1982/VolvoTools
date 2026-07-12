#pragma once

#include <type_traits>

template<typename E>
constexpr auto to_underlying(E e) -> typename std::underlying_type<E>::type
{
    return static_cast<typename std::underlying_type<E>::type>(e);
}
