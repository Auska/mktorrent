#ifndef MKTORRENT_MSG_H
#define MKTORRENT_MSG_H

#include "export.h"

/* let the compiler validate format strings of printf-like varargs functions */
#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_ATTR(fmt, first_vararg)                                                             \
	__attribute__((format(printf, fmt, first_vararg)))
#else
#define PRINTF_ATTR(fmt, first_vararg)
#endif

[[noreturn]] PRINTF_ATTR(1, 2) EXPORT void fatal(const char *format, ...);


#define FATAL_IF(cond, format, ...)                                                                \
	do {                                                                                       \
		if (cond)                                                                          \
			fatal(format, __VA_ARGS__);                                                \
	} while (0)
#define FATAL_IF0(cond, format)                                                                    \
	do {                                                                                       \
		if (cond)                                                                          \
			fatal(format);                                                             \
	} while (0)

#endif /* MKTORRENT_MSG_H */
