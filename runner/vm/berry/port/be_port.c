/* ChargeXcel port layer for Berry. Replaces upstream default/be_port.c.
 *
 * Two things live here. print() and friends go to dlm_script_write(), which
 * the host routes to its log. And every file-system entry point in be_sys.h is stubbed to
 * FAIL: a script that calls file() gets an io_error, be_loadfile() reports
 * "file not found", and nothing here can touch flash, NVS or the network.
 * BE_USE_FILE_SYSTEM is 0, but the `file` class is precompiled into the
 * builtin table regardless, so the stubs are what actually close the door.
 */
#include "berry.h"
#include "be_mem.h"
#include "be_sys.h"
#include <stddef.h>
#include <string.h>

void dlm_script_write(const char* buffer, size_t length);

BERRY_API void be_writebuffer(const char *buffer, size_t length)
{
    dlm_script_write(buffer, length);
}

BERRY_API char* be_readstring(char *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return NULL;
}

void* be_fopen(const char *filename, const char *modes)
{
    (void)filename;
    (void)modes;
    return NULL;
}

int be_fclose(void *hfile)
{
    (void)hfile;
    return -1;
}

size_t be_fwrite(void *hfile, const void *buffer, size_t length)
{
    (void)hfile;
    (void)buffer;
    (void)length;
    return 0;
}

size_t be_fread(void *hfile, void *buffer, size_t length)
{
    (void)hfile;
    (void)buffer;
    (void)length;
    return 0;
}

char* be_fgets(void *hfile, void *buffer, int size)
{
    (void)hfile;
    (void)buffer;
    (void)size;
    return NULL;
}

int be_fseek(void *hfile, long offset)
{
    (void)hfile;
    (void)offset;
    return -1;
}

long int be_ftell(void *hfile)
{
    (void)hfile;
    return -1;
}

long int be_fflush(void *hfile)
{
    (void)hfile;
    return -1;
}

size_t be_fsize(void *hfile)
{
    (void)hfile;
    return 0;
}

int be_isdir(const char *path)
{
    (void)path;
    return 0;
}

int be_isfile(const char *path)
{
    (void)path;
    return 0;
}

int be_isexist(const char *path)
{
    (void)path;
    return 0;
}

char* be_getcwd(char *buf, size_t size)
{
    if (buf && size)
        buf[0] = '\0';
    return buf;
}

int be_chdir(const char *path)
{
    (void)path;
    return -1;
}

int be_mkdir(const char *path)
{
    (void)path;
    return -1;
}

int be_unlink(const char *filename)
{
    (void)filename;
    return -1;
}

int be_dirfirst(bdirinfo *info, const char *path)
{
    (void)info;
    (void)path;
    return -1;
}

int be_dirnext(bdirinfo *info)
{
    (void)info;
    return -1;
}

int be_dirclose(bdirinfo *info)
{
    (void)info;
    return -1;
}
