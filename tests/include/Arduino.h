#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <chrono>
using namespace std::chrono;
class Stream { public: virtual ~Stream()=default; template<typename T> void println(const T&){(void)0;} template<typename T> void print(const T&){(void)0;} };
inline uint32_t micros(){static auto s=steady_clock::now();return (uint32_t)duration_cast<microseconds>(steady_clock::now()-s).count();}
inline uint32_t millis(){return micros()/1000;}
