#pragma once

#ifdef _WIN32
# ifdef argon_EXPORTS
#  define ARGON_DLL __declspec(dllexport)
# else
#  define ARGON_DLL __declspec(dllimport)
# endif
#else
# ifdef argon_EXPORTS
#  define ARGON_DLL __attribute__((visibility("default")))
# else
#  define ARGON_DLL
# endif
#endif