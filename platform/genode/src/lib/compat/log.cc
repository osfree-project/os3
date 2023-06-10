/* logging interface */

/* osFree internal */
#include <os3/io.h>

/* Genode includes */
#include <base/log.h>
#include <util/string.h>

/* libc includes */
#include <stdarg.h>
#include <stdio.h>

using namespace Genode;

extern "C"
void io_log(const char *chrFormat, ...)
{
    va_list arg_ptr;
    char buf[1024];
    char *s = (char *)buf;
    int len;

    va_start(arg_ptr, chrFormat);
    vsprintf(buf, chrFormat, arg_ptr);
    len = strlen((const char *)s);

    // strip a line feed at the end
    if (s[len - 1] == '\n')
    {
        s[len - 1] = '\0';
    }

    // output it
    log(Cstring(s));

    va_end(arg_ptr);
}
