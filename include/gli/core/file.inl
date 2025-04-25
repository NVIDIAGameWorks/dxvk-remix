#pragma once

#include <glm/simd/platform.h>

namespace gli{
namespace detail
{
	inline FILE* open_file(const char *Filename, const char *Mode)
	{
#		if GLM_COMPILER & GLM_COMPILER_VC
			FILE *File = nullptr;
			fopen_s(&File, Filename, Mode);
			return File;
#		else
			return std::fopen(Filename, Mode);
#		endif
	}

	// NV-DXVK Begin:
	// Workaround for REMIX-4014: fwrite() can cause problems for capture tests in CI depending on the CRT version, use Win32 instead API instead
	inline bool safe_write_win(const HANDLE hFile, const void* buffer, const DWORD bytes)
	{
		if (hFile == INVALID_HANDLE_VALUE || buffer == nullptr || bytes == 0) {
			dxvk::Logger::err("[GLI] Invalid arguments");
			return false;
		}

		DWORD written = 0;
		BOOL result = WriteFile(hFile, buffer, bytes, &written, nullptr);
		if (!result || written != bytes) {
			dxvk::Logger::err("[GLI] WriteFile failed: " + std::to_string(GetLastError()));
			return false;
		}
		return true;
	}
	// NV-DXVK End
}//namespace detail
}//namespace gli
