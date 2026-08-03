#ifndef HAIFY_DEBUG_H
#define HAIFY_DEBUG_H

#include <stdio.h>

/** Controls DEBUG_PRINT terminal output. */
extern bool gIsDebug;

/** Prints formatted debug output when gIsDebug is enabled. */
#define DEBUG_PRINT(fmt, ...)                                                  \
	do {                                                                       \
		if (gIsDebug)                                                          \
			printf("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__);         \
	} while (0)

#endif
