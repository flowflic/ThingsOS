// SPDX-License-Identifier: Apache-2.0
#include "thingsos/files.h"
#include "thingsos/mem.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#define equal _stricmp
#else
#include <dirent.h>
#include <strings.h>
#define equal strcasecmp
#endif
static bool logical(const char* path) {
    return path && (!strcmp(path, "/sdcard") || thingsos_user_file_allowed(path));
}
bool thingsos_book_path(const char* path) {
    if (!logical(path)) return false;
    const char* ext = strrchr(path, '.');
    return ext && (!equal(ext, ".epub") || !equal(ext, ".txt") || !equal(ext, ".md") || !equal(ext, ".markdown"));
}
static bool join(const char* path, const char* name, char out[THINGSOS_RECORD_PATH_BYTES]) {
    if (!logical(path) || !name || !name[0] || name[0] == '.' || strchr(name, '/') ||
        strchr(name, '\\') || strchr(name, ':') ||
        strlen(path) + strlen(name) + 2 > THINGSOS_RECORD_PATH_BYTES)
        return false;
    snprintf(out, THINGSOS_RECORD_PATH_BYTES, "%s/%s", path, name);
    return thingsos_user_file_allowed(out);
}
bool thingsos_browser_path(
    const thingsos_browser_t* b, size_t i, char out[THINGSOS_RECORD_PATH_BYTES]) {
    return b && i < b->count && join(b->path, b->entries[i].name, out);
}
static int compare(const void* left, const void* right) {
    const thingsos_file_entry_t *a = left, *b = right;
    if (a->directory != b->directory) return a->directory ? -1 : 1;
    return strcmp(a->name, b->name);
}
bool thingsos_directory_visible(const char* name) {
    if(!name || !name[0] || name[0]=='.')return false;
    const char* hidden[]={"whiteos","crosspoint","System Volume Information","$RECYCLE.BIN","RECYCLED","__MACOSX","LOST.DIR"};
    for(size_t i=0;i<sizeof(hidden)/sizeof(hidden[0]);++i)
        if(!equal(name,hidden[i]))return false;
    return true;
}
static void append(thingsos_browser_t* b, const char* name, bool directory, uint64_t size) {
    char path[THINGSOS_RECORD_PATH_BYTES];
    if(directory && !thingsos_directory_visible(name))return;
    if (strlen(name) >= 192 || !join(b->path, name, path)) return;
    if (b->count == THINGSOS_DIRECTORY_LIMIT) {
        b->truncated = true;
        return;
    }
    thingsos_file_entry_t* e = b->entries + b->count++;
    memset(e, 0, sizeof(*e));
    strcpy(e->name, name);
    e->directory = directory;
    e->size = size;
}
bool thingsos_browser_scan(thingsos_browser_t* b, const char* root, const char* path) {
    if (!b || !root || !logical(path) || strlen(root) > 400 ||
        strlen(path) >= THINGSOS_RECORD_PATH_BYTES)
        return false;
    thingsos_browser_t* next = thingsos_psram_calloc(1, sizeof(*next));
    if (!next) return false;
    strcpy(next->path, path);
    char actual[1024];
    snprintf(actual, sizeof(actual), "%s%s", root, path + 7);
#ifdef _WIN32
    wchar_t pattern[1024];
    char search[1030];
    snprintf(search, sizeof(search), "%s/*", actual);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, search, -1, pattern, 1024)) {
        free(next);
        return false;
    }
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            free(next);
            return false;
        }
    } else {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            char name[192];
            if (!WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, data.cFileName, -1, name, sizeof(name), NULL,
                    NULL))
                continue;
            append(
                next, name, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow);
        } while (FindNextFileW(handle, &data));
        DWORD error = GetLastError();
        FindClose(handle);
        if (error != ERROR_NO_MORE_FILES) {
            free(next);
            return false;
        }
    }
#else
    DIR* directory = opendir(actual);
    if (!directory) {
        free(next);
        return false;
    }
    struct dirent* entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.' || strlen(entry->d_name) >= 192) continue;
        char child[THINGSOS_RECORD_PATH_BYTES], disk[1024];
        if (!join(path, entry->d_name, child)) continue;
        snprintf(disk, sizeof(disk), "%s%s", root, child + 7);
        struct stat info;
#ifdef ESP_PLATFORM
        if (stat(disk, &info)) continue;
#else
        if (lstat(disk, &info) || S_ISLNK(info.st_mode)) continue;
#endif
        if (S_ISDIR(info.st_mode) || S_ISREG(info.st_mode))
            append(next, entry->d_name, S_ISDIR(info.st_mode), (uint64_t)info.st_size);
    }
    closedir(directory);
#endif
    qsort(next->entries, next->count, sizeof(next->entries[0]), compare);
    *b = *next;
    free(next);
    return true;
}
static bool name_matches(const char* name,const char* query) {
    if(!query || !query[0])return true;
    size_t n=strlen(query);
    for(const unsigned char* p=(const unsigned char*)name;*p;++p) {
        size_t i=0;while(i<n && p[i] && tolower(p[i])==tolower((unsigned char)query[i]))++i;
        if(i==n)return true;
    }
    return false;
}
size_t thingsos_browser_matches(const thingsos_browser_t* b,const char* query) {
    size_t n=0;if(b)for(size_t i=0;i<b->count;++i)n+=name_matches(b->entries[i].name,query);return n;
}
size_t thingsos_browser_match(const thingsos_browser_t* b,const char* query,size_t row) {
    if(b)for(size_t i=0;i<b->count;++i)if(name_matches(b->entries[i].name,query)){if(!row)return i;--row;}
    return SIZE_MAX;
}
bool thingsos_browser_enter(thingsos_browser_t* b, const char* root, size_t i) {
    char path[THINGSOS_RECORD_PATH_BYTES];
    return b && i < b->count && b->entries[i].directory && thingsos_browser_path(b, i, path) &&
           thingsos_browser_scan(b, root, path);
}
bool thingsos_browser_parent(thingsos_browser_t* b, const char* root) {
    if (!b || !strcmp(b->path, "/sdcard")) return false;
    char path[THINGSOS_RECORD_PATH_BYTES];
    strcpy(path, b->path);
    char* slash = strrchr(path, '/');
    if (!slash || slash <= path + 6) return false;
    *slash = 0;
    return thingsos_browser_scan(b, root, path);
}

bool thingsos_collision_name(const char* name, unsigned copy, char out[192]) {
    if (!name || !out || !copy || copy >= 10000 || strlen(name) >= 192 || name[0] == '.' ||
        strpbrk(name, "/\\:*?\"<>|"))
        return false;
    const char* ext = strrchr(name, '.');
    if (!ext || strlen(ext) > 8) return false;
    char suffix[24];
    int n = snprintf(suffix, sizeof(suffix), " (%u)%s", copy, ext);
    if (n < 0 || n > 23) return false;
    size_t prefix = (size_t)(ext - name), max = 191 - (size_t)n;
    if (prefix > max) {
        prefix = max;
        while (prefix && ((unsigned char)name[prefix] & 192) == 128) --prefix;
    }
    if (!prefix) return false;
    memcpy(out, name, prefix);
    memcpy(out + prefix, suffix, (size_t)n + 1);
    return true;
}
