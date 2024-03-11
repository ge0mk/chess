#pragma once

#include <format>
#include <iostream>

template <typename ...Args>
static inline void print(std::format_string<Args...> fmt, Args &&...args) {
	std::cout<<std::format(fmt, std::forward<Args>(args)...);
}

template <typename ...Args>
static inline void println(std::format_string<Args...> fmt, Args &&...args) {
	std::cout<<std::format(fmt, std::forward<Args>(args)...)<<std::endl;
}

template <typename ...Args>
static inline void eprint(std::format_string<Args...> fmt, Args &&...args) {
	std::cerr<<std::format(fmt, std::forward<Args>(args)...);
}

template <typename ...Args>
static inline void eprintln(std::format_string<Args...> fmt, Args &&...args) {
	std::cerr<<std::format(fmt, std::forward<Args>(args)...)<<std::endl;
}

template <typename ...Args>
static inline void panic(std::format_string<Args...> fmt, Args &&...args) {
	std::cerr<<std::format(fmt, std::forward<Args>(args)...)<<std::endl;
	abort();
}
