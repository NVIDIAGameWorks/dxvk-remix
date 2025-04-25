/// @brief File helper functions
/// @file gli/core/file.hpp

#pragma once

#include <cstdio>

namespace gli{
namespace detail
{
	FILE* open_file(const char *Filename, const char *mode);

	// NV-DXVK Begin
	bool safe_write_win(HANDLE hFile, const void* buffer, DWORD bytes);
	// NV-DXVK End
}//namespace detail
}//namespace gli

#include "./file.inl"
