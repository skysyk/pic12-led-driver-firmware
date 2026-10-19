/* POSIX string helpers used by upstream but absent from MinGW's CRT. */
#ifndef MINIPRO_WINDOWS_COMPAT_H
#define MINIPRO_WINDOWS_COMPAT_H
#ifdef _WIN32
#include <stdlib.h>
#include <string.h>
static inline char *minipro_windows_strndup(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0')
        ++length;
    char *copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}
#define strndup minipro_windows_strndup
static inline char *minipro_windows_stpcpy(char *destination, const char *source)
{
    size_t length = strlen(source);
    memcpy(destination, source, length + 1);
    return destination + length;
}
#define stpcpy minipro_windows_stpcpy
#endif
#endif
