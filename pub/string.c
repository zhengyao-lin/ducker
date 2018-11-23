#include "pub/string.h"

bool
string_endswith(const char *str, const char *suf)
{
    size_t l1 = strlen(str);
    size_t l2 = strlen(suf);

    const char *sub;

    if (l1 < l2) return false;

    sub = str + (l1 - l2);

    return memcmp(sub, suf, l2) == 0;
}
