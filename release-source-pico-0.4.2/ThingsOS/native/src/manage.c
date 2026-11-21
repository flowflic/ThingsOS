// SPDX-License-Identifier: Apache-2.0
#include "thingsos/manage.h"
#include "thingsos/mem.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "thingsos/input.h"
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define file_sync(f) _commit(_fileno(f))
static bool wide(const char* s, wchar_t out[1024]) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, 1024) > 0;
}
static int move_file(const char* a, const char* b) {
    wchar_t x[1024], y[1024];
    return wide(a, x) && wide(b, y) && MoveFileW(x, y) ? 0 : -1;
}
static int remove_file(const char* a) {
    wchar_t x[1024];
    return wide(a, x) && DeleteFileW(x) ? 0 : -1;
}
static int regular(const char* a) {
    wchar_t x[1024];
    if (!wide(a, x)) return -1;
    DWORD flags = GetFileAttributesW(x);
    return flags == INVALID_FILE_ATTRIBUTES                                      ? 0
           : (flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ? -1
                                                                                 : 1;
}
#else
#include <unistd.h>
#define file_sync(f) fsync(fileno(f))
static int move_file(const char* a, const char* b) {
    return rename(a, b);
}
static int remove_file(const char* a) {
    return unlink(a);
}
static int regular(const char* a) {
    struct stat s;
#ifdef ESP_PLATFORM
    if (stat(a, &s)) return errno == ENOENT ? 0 : -1;
#else
    if (lstat(a, &s)) return errno == ENOENT ? 0 : -1;
#endif
    return S_ISREG(s.st_mode) ? 1 : -1;
}
#endif
static bool same_extension(const char* a, const char* b) {
    while (*a && *b) {
        if (((*a++) | 32) != ((*b++) | 32)) return false;
    }
    return !*a && !*b;
}
static thingsos_path_mapper_t path_mapper;
void thingsos_path_mapper(thingsos_path_mapper_t mapper) {path_mapper=mapper;}
uint64_t thingsos_path_id(const char* s) {
    char canonical[512];if(path_mapper && path_mapper(s,canonical,sizeof(canonical)))s=canonical;
    uint64_t v = 1469598103934665603ULL;
    for (; s && *s; ++s) v = (v ^ (unsigned char)*s) * 1099511628211ULL;
    return v ? v : 1;
}
bool thingsos_book_name(const char* s) {
    if (!s || !*s || *s == '.' || *s == ' ' || strlen(s) > 96 || strchr(s, '/') ||
        strchr(s, '\\') || strpbrk(s, ":*?\"<>|"))
        return false;
    size_t n = strlen(s);
    if (s[n - 1] == ' ' || s[n - 1] == '.') return false;
    thingsos_input_t i;
    char path[256];
    snprintf(path, sizeof(path), "/sdcard/%s", s);
    return thingsos_input_open(&i, s, 96, false) && thingsos_book_path(path);
}
bool thingsos_remote_directory(const char* p) {
    if(!p)return false;
    if(!strcmp(p,"/sdcard"))return true;
    if(!thingsos_user_file_allowed(p) || strlen(p)>=512)return false;
    const char* part=p+8;
    while(*part) {
        const char* end=strchr(part,'/');size_t n=end?(size_t)(end-part):strlen(part);
        /* FAT can normalize trailing dots/spaces; reject aliases before
           comparing protected directory names or accepting a file path. */
        if(!n || n>=256 || *part=='.' || *part==' ' || part[n-1]=='.' || part[n-1]==' ')return false;
        char name[256];memcpy(name,part,n);name[n]=0;
        if(!thingsos_directory_visible(name) || strpbrk(name,":*?\"<>|"))return false;
        const char* system[]={"config","system","firmware","cache","caches","logs"};
        for(size_t i=0;i<sizeof(system)/sizeof(system[0]);++i)
            if(same_extension(name,system[i]))return false;
        if(!end)break;
        part=end+1;
    }
    return true;
}
bool thingsos_managed_name(const char* name) {
    if(!name || !*name || *name=='.' || *name==' ' || strlen(name)>96 ||
       strpbrk(name,"/\\:*?\"<>|"))return false;
    size_t n=strlen(name);if(name[n-1]==' ' || name[n-1]=='.')return false;
    thingsos_input_t input;
    if(!thingsos_input_open(&input,name,96,false))return false;
    const char* ext=strrchr(name,'.');
    return ext && (same_extension(ext,".epub") || same_extension(ext,".txt") || same_extension(ext,".md") || same_extension(ext,".markdown") ||
        same_extension(ext,".png") || same_extension(ext,".jpg") || same_extension(ext,".jpeg"));
}
bool thingsos_managed_path(const char* p) {
    if(!p || !thingsos_remote_directory(p))return false;
    const char* name=strrchr(p,'/');
    if(!name)return false;
    /* Existing uploads can have names up to 190 bytes; allow deleting and
       previewing those too, while new names retain the UI's 96-byte limit. */
    const char* ext=strrchr(name,'.');
    return ext && (same_extension(ext,".epub") || same_extension(ext,".txt") || same_extension(ext,".md") || same_extension(ext,".markdown") ||
        same_extension(ext,".png") || same_extension(ext,".jpg") || same_extension(ext,".jpeg"));
}
static bool protected_path(const char* p) {
    return !thingsos_managed_path(p);
}

typedef struct {
    char magic[8];
    uint64_t card, generation, checksum;
    char source[512], target[512];
    bool deleting;
} management_journal_t;
static uint64_t journal_hash(const management_journal_t* j) {
    const unsigned char* b = (const unsigned char*)j;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < sizeof(*j); ++i)
        h = (h ^ (i >= 24 && i < 32 ? 0 : b[i])) * 1099511628211ULL;
    return h;
}
static bool journal_write(
    const char* root, const thingsos_records_t* r, const char* path, const char* target,
    bool deleting) {
    management_journal_t* j = thingsos_psram_calloc(1, sizeof(*j));
    if (!j) return false;
    memcpy(j->magic, "THMNG001", 8);
    j->card = r->card_id;
    j->generation = r->generation + 1;
    strcpy(j->source, path);
    strcpy(j->target, target + strlen(root));
    j->deleting = deleting;
    j->checksum = journal_hash(j);
    char disk[512];
    snprintf(disk, sizeof(disk), "%s/.thingsos/manage-journal.dat", root);
    FILE* f = fopen(disk, "wb");
    bool ok = f != NULL;
    if (f) {
        if (fwrite(j, 1, sizeof(*j), f) != sizeof(*j) || fflush(f) || file_sync(f)) ok = false;
        if (fclose(f)) ok = false;
    }
    if (ok) {
        management_journal_t* check = thingsos_psram_alloc(sizeof(*check));
        f = fopen(disk, "rb");
        ok = check && f && fread(check, 1, sizeof(*check), f) == sizeof(*check) &&
             !memcmp(check, j, sizeof(*j));
        if (f) fclose(f);
        free(check);
    }
    free(j);
    return ok;
}
static void journal_clear(const char* root) {
    char path[512];
    snprintf(path, sizeof(path), "%s/.thingsos/manage-journal.dat", root);
    remove_file(path);
}
bool thingsos_manage_recover(thingsos_records_t* r, const char* root) {
    if (!r || !root || strlen(root) > 400) return false;
    char path[512];
    snprintf(path, sizeof(path), "%s/.thingsos/manage-journal.dat", root);
    FILE* f = fopen(path, "rb");
    if (!f) return errno == ENOENT;
    management_journal_t* j = thingsos_psram_calloc(1, sizeof(*j));
    if (!j) {
        fclose(f);
        return false;
    }
    bool ok = fread(j, 1, sizeof(*j), f) == sizeof(*j) && fgetc(f) == EOF;
    fclose(f);
    ok = ok && !memcmp(j->magic, "THMNG001", 8) && j->card == r->card_id &&
         j->checksum == journal_hash(j) && memchr(j->source, 0, 512) && memchr(j->target, 0, 512) &&
         !protected_path(j->source);
    char source[1024], target[1024], expected[512];
    if (ok) {
        snprintf(source, sizeof(source), "%s%s", root, j->source + 7);
        snprintf(target, sizeof(target), "%s%s", root, j->target);
        size_t prefix = (size_t)(strrchr(j->source, '/') - j->source) + 1;
        if (j->deleting) {
            snprintf(
                expected, sizeof(expected), "%.*s.deleted-%016llx-%llu.tmp", (int)(prefix - 7),
                j->source + 7, (unsigned long long)thingsos_path_id(j->source),
                (unsigned long long)(j->generation - 1));
            ok = !strcmp(expected, j->target);
        } else {
            char logical[1024];
            snprintf(logical, sizeof(logical), "/sdcard%s", j->target);
            ok = thingsos_managed_name(strrchr(logical, '/') + 1) && !protected_path(logical) &&
                 !strncmp(j->source, logical, prefix);
        }
    }
    if (ok) {
        int a = regular(source), b = regular(target);
        if (r->generation >= j->generation) {
            if (j->deleting && b == 1)
                ok = remove_file(target) == 0;
            else if (!j->deleting)
                ok = a == 0 && b == 1;
        } else {
            if (a == 0 && b == 1)
                ok = move_file(target, source) == 0;
            else
                ok = a == 1 && b == 0;
        }
    }
    if (ok) journal_clear(root);
    free(j);
    return ok;
}
thingsos_manage_result_t thingsos_book_manage(
    thingsos_records_t* r, const char* root, const char* path, const char* new_name) {
    if (!r || !root || strlen(root) > 400 || !path || strlen(path) >= 512 ||
        r->generation == UINT64_MAX || protected_path(path) ||
        (new_name && !thingsos_managed_name(new_name)))
        return THINGSOS_MANAGE_INVALID;
    if (!thingsos_manage_recover(r, root)) return THINGSOS_MANAGE_RESTORE_FAILED;
    char source[1024], target[1024], logical[512];
    snprintf(source, sizeof(source), "%s%s", root, path + 7);
    if (regular(source) != 1) return THINGSOS_MANAGE_IO;
    size_t prefix = (size_t)(strrchr(path, '/') - path) + 1;
    if (new_name) {
        if (prefix + strlen(new_name) >= sizeof(logical)) return THINGSOS_MANAGE_INVALID;
        memcpy(logical, path, prefix);
        strcpy(logical + prefix, new_name);
        if (!strcmp(path, logical)) return THINGSOS_MANAGE_OK;
        snprintf(target, sizeof(target), "%s%s", root, logical + 7);
    } else {
        logical[0] = 0;
        snprintf(
            target, sizeof(target), "%s%.*s.deleted-%016llx-%llu.tmp", root, (int)(prefix - 7),
            path + 7, (unsigned long long)thingsos_path_id(path),
            (unsigned long long)r->generation);
    }
    if (strlen(target + strlen(root)) >= 512) return THINGSOS_MANAGE_INVALID;
    if (new_name && !same_extension(strrchr(path, '.'), strrchr(new_name, '.')))
        return THINGSOS_MANAGE_INVALID;
    if (regular(target) != 0) return THINGSOS_MANAGE_EXISTS;
    uint64_t old = thingsos_path_id(path), next = new_name ? thingsos_path_id(logical) : old;
    if (new_name && next != old && thingsos_records_book(r, next, NULL, NULL))
        return THINGSOS_MANAGE_EXISTS;
    thingsos_records_t* copy = thingsos_psram_alloc(sizeof(*copy));
    if (!copy) return THINGSOS_MANAGE_IO;
    *copy = *r;
    thingsos_book_record_t* b = thingsos_records_book(copy, old, NULL, NULL);
    if (!b && new_name && thingsos_book_path(path)) {
        b = thingsos_records_book(copy, old, path, new_name);
        if (!b) {
            free(copy);
            return THINGSOS_MANAGE_STORE;
        }
    }
    if (b) {
        if (new_name) {
            b->id = next;
            strcpy(b->path, logical);
            strcpy(b->title, new_name);
        } else {
            b->order = 0;
            b->chapter = b->offset = b->page = b->pages = b->percent = 0;
        }
    }
    for (size_t i = 0; i < copy->mark_count;) {
        if (copy->marks[i].book_id == old) {
            if (new_name) {
                copy->marks[i++].book_id = next;
            } else {
                memmove(
                    copy->marks + i, copy->marks + i + 1,
                    (copy->mark_count - i - 1) * sizeof(copy->marks[0]));
                --copy->mark_count;
            }
        } else
            ++i;
    }
    for (size_t i = 0; i < copy->book_day_count; ++i)
        if (copy->book_days[i].id == old) copy->book_days[i].id = next;
    thingsos_manage_result_t result = THINGSOS_MANAGE_OK;
    if (!journal_write(root, r, path, target, !new_name))
        result = THINGSOS_MANAGE_STORE;
    else if (move_file(source, target)) {
        result = THINGSOS_MANAGE_IO;
        journal_clear(root);
    } else if (!thingsos_store_save_compatible(copy, root)) {
        result = move_file(target, source) ? THINGSOS_MANAGE_RESTORE_FAILED : THINGSOS_MANAGE_STORE;
        if (result == THINGSOS_MANAGE_STORE) journal_clear(root);
    } else {
        *r = *copy;
        if (new_name || remove_file(target) == 0) journal_clear(root);
    }
    free(copy);
    return result;
}
