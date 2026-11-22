// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_directory(p) _mkdir(p)
#define remove_directory(p) _rmdir(p)
#else
#include <unistd.h>
#define make_directory(p) mkdir(p, 0775)
#define remove_directory(p) rmdir(p)
#endif
#include "thingsos/files.h"
#include "thingsos/ime.h"
#include "thingsos/input.h"
#include "thingsos/manage.h"
#include "thingsos/stats.h"
#define CHECK(x)                                               \
    do {                                                       \
        if (!(x)) {                                            \
            fprintf(stderr, "Failure %d: %s\n", __LINE__, #x); \
            exit(1);                                           \
        }                                                      \
    } while (0)
static void slot(char out[600], const char* root, unsigned n) {
    snprintf(out, 600, "%s/.thingsos/state-%u.dat", root, n);
}
/* Create an interrupted filesystem transaction, as if power failed between
   the staged move and the durable base-record commit. */
static void interrupted_journal(
    const char* root, const thingsos_records_t* r, const char* source, const char* target,
    bool committed_delete) {
    struct journal {
        char magic[8];
        uint64_t card, generation, checksum;
        char source[512], target[512];
        bool deleting;
    } j = { 0 };
    memcpy(j.magic, "THMNG001", 8);
    j.card = r->card_id;
    j.generation = r->generation + (committed_delete ? 0 : 1);
    j.deleting = committed_delete;
    strcpy(j.source, source);
    strcpy(j.target, target);
    const unsigned char* bytes = (const unsigned char*)&j;
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < sizeof(j); ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    j.checksum = hash;
    char path[600];
    snprintf(path, sizeof(path), "%s/.thingsos/manage-journal.dat", root);
    FILE* file = fopen(path, "wb");
    CHECK(file);
    CHECK(fwrite(&j, 1, sizeof(j), file) == sizeof(j));
    fclose(file);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const char* root = argv[1];
    thingsos_input_t input;
    char displayed[200];
    CHECK(thingsos_input_open(&input, "中文WiFi", 32, false));
    thingsos_input_move(&input, -1);
    CHECK(thingsos_input_insert(&input, "X"));
    CHECK(!strcmp(input.text, "中文WiFXi"));
    thingsos_input_move(&input, -1);
    thingsos_input_backspace(&input);
    CHECK(!strcmp(input.text, "中文WiXi"));
    CHECK(thingsos_input_open(&input, "中文", 6, true));
    thingsos_input_move(&input, -1);
    thingsos_input_backspace(&input);
    CHECK(!strcmp(input.text, "文") && input.cursor == 0);
    CHECK(thingsos_input_insert(&input, "中"));
    CHECK(!thingsos_input_insert(&input, "!"));
    CHECK(!thingsos_input_insert(&input, "\xc0\x80"));
    CHECK(!thingsos_input_insert(&input, "\n"));
    thingsos_input_display(&input, displayed, sizeof(displayed));
    CHECK(!strstr(displayed, "中文") && !strcmp(displayed, "*|*"));
    input.reveal = true;
    thingsos_input_display(&input, displayed, sizeof(displayed));
    CHECK(!strcmp(displayed, "中|文"));
    CHECK(
        thingsos_input_key_hit(36, 600) == 0 && thingsos_input_key_hit(35, 600) == -1 &&
        thingsos_input_key_hit(645, 1043) == -1);
    CHECK(!strcmp(thingsos_input_key(&input, 39), "9"));
    input.mode = THINGSOS_KEYS_SYMBOLS;
    CHECK(!strcmp(thingsos_input_key(&input, 17), "\\"));
    for (unsigned key = 0; key < 49; key++)
        CHECK(
            thingsos_input_key_hit(36 + (key % 10) * 61 + 20, 600 + (key / 10) * 74 + 30) ==
            (int)key);
    thingsos_records_t *r = malloc(sizeof(*r)), *restored = malloc(sizeof(*restored));
    CHECK(r && restored);
    thingsos_records_init(r);
    r->card_id = 1234;
    thingsos_book_record_t* book =
        thingsos_records_book(r, 22, "/sdcard/books/目录/故事.txt", "故事");
    CHECK(book);
    book->chapter = 3;
    book->offset = 12;
    book->page = 4;
    book->pages = 50;
    book->percent = 6;
    book->order = ++r->order;
    CHECK(thingsos_records_time(r, 22, 20261011, 3000));
    CHECK(thingsos_records_hour(r,20261011,10,1000));
    CHECK(thingsos_records_hour(r,20261011,11,2000));
    CHECK(!thingsos_records_hour(r,20260230,10,1) && !thingsos_records_hour(r,20261011,24,1));
    r->marks[r->mark_count++] = (thingsos_location_t){ 22, 3, 12 };
    char p[600];
    slot(p, root, 0);
    remove(p);
    slot(p, root, 1);
    remove(p);
    CHECK(thingsos_store_load(restored, root) == THINGSOS_STORE_MISSING);
    CHECK(thingsos_store_save_compatible(r, root) && r->generation == 1);
    slot(p, root, 1);
    FILE* compatible = fopen(p, "rb");
    char signature[8];
    CHECK(
        compatible && fread(signature, 1, 8, compatible) == 8 && !memcmp(signature, "THSTATE1", 8));
    fclose(compatible);
    CHECK(
        thingsos_store_load(restored, root) == THINGSOS_STORE_OK && restored->total_ms == 3000 &&
        restored->marks[0].chapter == 3 && !strcmp(restored->books[0].path, book->path));
    CHECK(restored->hour_date==20261011 && restored->hour_ms[10]==1000 && restored->hour_ms[11]==2000);
    CHECK(thingsos_records_time(r, 22, 0, 1000));
    CHECK(thingsos_records_time(r, 22, 20261012, 1500));
    CHECK(thingsos_records_hour(r,20261012,12,1500) && !r->hour_ms[10]);
    CHECK(thingsos_store_save(r, root) && r->generation == 2);
    CHECK(
        thingsos_store_load(restored, root) == THINGSOS_STORE_OK && restored->undated_ms == 1000 &&
        restored->day_count == 2 && restored->total_ms == 5500);
    CHECK(restored->hour_date==20261012 && restored->hour_ms[12]==1500);
    snprintf(p,sizeof(p),"%s/.thingsos/hours-0.dat",root);
    FILE* hours=fopen(p,"r+b");CHECK(hours);CHECK(fseek(hours,40,SEEK_SET)==0);CHECK(fputc(255,hours)!=EOF);fclose(hours);
    CHECK(thingsos_store_load(restored,root)==THINGSOS_STORE_OK && restored->total_ms==5500 && !restored->hour_date);
    slot(p, root, 0);
    FILE* f = fopen(p, "r+b");
    CHECK(f);
    CHECK(fseek(f, 30, SEEK_SET) == 0);
    CHECK(fputc(255, f) != EOF);
    fclose(f);
    CHECK(
        thingsos_store_load(restored, root) == THINGSOS_STORE_RECOVERED &&
        restored->generation == 1 && restored->total_ms == 3000 && restored->hour_date==20261011 && restored->hour_ms[11]==2000);
    CHECK(thingsos_store_save(restored, root));
    CHECK(thingsos_store_load(r, root) == THINGSOS_STORE_OK && r->generation == 2);
    r->books[0].page = 4;
    r->books[0].pages = 50;
    r->books[0].percent = 6;
    unsigned char* wire = malloc(THINGSOS_STORE_BYTES);
    size_t bytes;
    CHECK(wire && thingsos_records_encode(r, wire, THINGSOS_STORE_BYTES, &bytes));
    CHECK(
        thingsos_records_decode(restored, wire, bytes) && restored->books[0].page == 4 &&
        restored->books[0].percent == 6);
    /* Convert a one-book snapshot to the previous format, including a valid CRC.
       Existing SD records must survive the firmware upgrade. */
    CHECK(r->book_count == 1);
    unsigned char* legacy = malloc(bytes);
    CHECK(legacy);
    memcpy(legacy, wire, 612);
    memcpy(legacy + 612, wire + 624, bytes - 624);
    memcpy(legacy, "THSTATE1", 8);
    size_t legacy_bytes = bytes - 12;
    for (unsigned i = 0; i < 4; ++i) legacy[16 + i] = (unsigned char)(legacy_bytes >> (8 * i));
    uint32_t legacy_crc = UINT32_MAX;
    for (size_t i = 0; i < legacy_bytes; ++i) {
        legacy_crc ^= i >= 20 && i < 24 ? 0 : legacy[i];
        for (unsigned j = 0; j < 8; ++j)
            legacy_crc = (legacy_crc >> 1) ^ (0xedb88320u & (0u - (legacy_crc & 1)));
    }
    legacy_crc = ~legacy_crc;
    for (unsigned i = 0; i < 4; ++i) legacy[20 + i] = (unsigned char)(legacy_crc >> (8 * i));
    CHECK(thingsos_records_decode(restored, legacy, legacy_bytes));
    CHECK(
        restored->books[0].offset == 12 && restored->books[0].page == 0 &&
        restored->total_ms == r->total_ms);
    free(legacy);
    for (size_t n = 0; n < bytes; n += 37) CHECK(!thingsos_records_decode(restored, wire, n));
    wire[8] ^= 1;
    CHECK(!thingsos_records_decode(restored, wire, bytes));
    wire[8] ^= 1;
    CHECK(thingsos_records_decode(restored, wire, bytes));
    CHECK(!thingsos_records_book(r, 99, "/sdcard/../flash.txt", "bad"));
    CHECK(!thingsos_records_book(r, 99, "/flash/file.txt", "bad"));
    CHECK(!thingsos_records_time(r, 22, 20260229, 1));
    CHECK(thingsos_date_valid(20240229) && !thingsos_date_valid(20241301));
    for (unsigned day = 20200101; day <= 20200131; day++)
        CHECK(thingsos_records_time(r, 22, day, 1));
    CHECK(thingsos_records_book(r, 1, "builtin:example", "Example Test"));
    CHECK(thingsos_records_time(r, 1, 0, 77));
    CHECK(
        thingsos_records_encode(r, wire, THINGSOS_STORE_BYTES, &bytes) &&
        thingsos_records_decode(restored, wire, bytes));
    /* A failed writer cannot change the accepted in-memory generation or destroy the other slot. */
    uint64_t generation = r->generation;
    CHECK(!thingsos_store_save(r, "Z:/nonexistent-thingsos-path") && r->generation == generation);
    /* Fail exactly the inactive slot: the accepted slot must still restore. */
    slot(p, root, (unsigned)((generation + 1) & 1));
    CHECK(remove(p) == 0);
    CHECK(make_directory(p) == 0);
    CHECK(!thingsos_store_save(r, root) && r->generation == generation);
    CHECK(
        thingsos_store_load(restored, root) == THINGSOS_STORE_RECOVERED &&
        restored->generation == generation);
    CHECK(remove_directory(p) == 0);
    CHECK(thingsos_store_save(r, root));
    /* Bookmark cancellation is persisted, and both corrupt slots preserve caller state. */
    r->mark_count = 0;
    CHECK(thingsos_store_save(r, root));
    CHECK(thingsos_store_load(restored, root) == THINGSOS_STORE_OK && restored->mark_count == 0);
    for (unsigned i = 0; i < 2; i++) {
        slot(p, root, i);
        FILE* bad = fopen(p, "wb");
        CHECK(bad);
        CHECK(fwrite("torn", 1, 4, bad) == 4);
        fclose(bad);
    }
    uint64_t intact = restored->total_ms;
    CHECK(
        thingsos_store_load(restored, root) == THINGSOS_STORE_CORRUPT &&
        restored->total_ms == intact);
    /* Retain the latest 64 distinct dates, while lifetime totals stay intact. */
    thingsos_records_init(r);
    CHECK(thingsos_records_book(r, 22, "/sdcard/books/a.txt", "a"));
    for (unsigned month = 1; month <= 3; month++)
        for (unsigned day = 1; day <= 28; day++)
            CHECK(thingsos_records_time(r, 22, 20260000 + month * 100 + day, 1000));
    CHECK(r->day_count == 64 && r->total_ms == 84000);
    for (size_t i = 0; i < r->day_count; i++) CHECK(r->days[i].date >= 20260121);
    CHECK(thingsos_records_time(r, 22, 20200101, 77) && r->day_count == 64 && r->total_ms == 84077);
    CHECK(
        thingsos_records_encode(r, wire, THINGSOS_STORE_BYTES, &bytes) &&
        thingsos_records_decode(restored, wire, bytes));
    thingsos_browser_t* browser = malloc(sizeof(*browser));
    CHECK(browser && thingsos_browser_scan(browser, root, "/sdcard"));
    CHECK(!thingsos_directory_visible("WhiteOS") && !thingsos_directory_visible("whiteos") &&
        !thingsos_directory_visible("System Volume Information") && !thingsos_directory_visible("$RECYCLE.BIN"));
    CHECK(thingsos_directory_visible("books") && thingsos_directory_visible("fonts") && thingsos_directory_visible("wallpapers"));
    size_t nested = SIZE_MAX;
    for (size_t i = 0; i < browser->count; i++) {
        CHECK(browser->entries[i].name[0] != '.');
        CHECK(thingsos_directory_visible(browser->entries[i].name));
        if (!strcmp(browser->entries[i].name, "books")) nested = i;
    }
    CHECK(nested != SIZE_MAX && thingsos_browser_enter(browser, root, nested));
    CHECK(!strcmp(browser->path, "/sdcard/books"));
    for(unsigned i=0;i<300;++i){snprintf(p,sizeof(p),"%s/books/FolderBook-%03u.txt",root,i);FILE* book=fopen(p,"wb");CHECK(book);fputs("sample",book);CHECK(!fclose(book));}
    CHECK(thingsos_browser_scan(browser,root,"/sdcard/books") && browser->count>=300 && !browser->truncated);
    CHECK(thingsos_browser_matches(browser,"folderbook-")==300 && thingsos_browser_matches(browser,"FolderBook-299")==1);
    size_t found=thingsos_browser_match(browser,"FolderBook-299",0);CHECK(found<browser->count && !strcmp(browser->entries[found].name,"FolderBook-299.txt"));
    CHECK(thingsos_browser_match(browser,"no-match",0)==SIZE_MAX);
    CHECK(thingsos_browser_parent(browser, root) && !strcmp(browser->path, "/sdcard"));
    CHECK(!thingsos_browser_parent(browser, root));
    CHECK(!thingsos_browser_scan(browser, root, "/sdcard/../flash"));
    CHECK(thingsos_book_path("/sdcard/故事.EPUB") && !thingsos_book_path("/sdcard/a.png"));
    CHECK(thingsos_browser_scan(browser, root, "/sdcard/empty") && browser->count == 0);
    CHECK(
        !thingsos_browser_scan(browser, root, "/sdcard/missing") &&
        !strcmp(browser->path, "/sdcard/empty"));
    /* Real filesystem transaction: UTF-8 rename, collision, failed save undo, delete. */
    for (unsigned i = 0; i < 2; ++i) {
        slot(p, root, i);
        remove(p);
    }
    thingsos_records_init(r);
    r->card_id = 91;
    char original[600];
    snprintf(original, sizeof(original), "%s/books/manage.txt", root);
    FILE* managed_file = fopen(original, "wb");
    CHECK(managed_file);
    CHECK(fwrite("managed", 1, 7, managed_file) == 7);
    fclose(managed_file);
    uint64_t id = thingsos_path_id("/sdcard/books/manage.txt");
    thingsos_book_record_t* managed =
        thingsos_records_book(r, id, "/sdcard/books/manage.txt", "manage.txt");
    CHECK(managed);
    managed->order = ++r->order;
    managed->chapter = 2;
    managed->offset = 77;
    r->marks[r->mark_count++] = (thingsos_location_t){ id, 2, 77 };
    CHECK(thingsos_records_time(r, id, 20261012, 33000));
    CHECK(thingsos_store_save_compatible(r, root));
    CHECK(
        thingsos_book_time(r, id, 20261012, 20261012) == 33000 &&
        thingsos_book_time(r, id, 20261011, 20261011) == 0);
    CHECK(
        thingsos_store_load(restored, root) == THINGSOS_STORE_OK &&
        thingsos_book_time(restored, id, 20261012, 20261012) == 33000);
    CHECK(thingsos_book_manage(r, root, "builtin:example", NULL) == THINGSOS_MANAGE_INVALID);
    CHECK(
        thingsos_book_manage(r, root, "/sdcard/.thingsos/state.txt", NULL) ==
        THINGSOS_MANAGE_INVALID);
    CHECK(
        thingsos_book_manage(r, root, "/sdcard/books/manage.txt", "../escape.txt") ==
        THINGSOS_MANAGE_INVALID);
    CHECK(
        thingsos_book_manage(r, root, "/sdcard/books/manage.txt", "wrong.epub") ==
        THINGSOS_MANAGE_INVALID);
    snprintf(p, sizeof(p), "%s/books/collision.txt", root);
    FILE* collision = fopen(p, "wb");
    CHECK(collision);
    fputs("original", collision);
    fclose(collision);
    CHECK(
        thingsos_book_manage(r, root, "/sdcard/books/manage.txt", "collision.txt") ==
        THINGSOS_MANAGE_EXISTS);
    uint64_t before = r->generation;
    slot(p, root, (unsigned)((before + 1) & 1));
    remove(p);
    CHECK(make_directory(p) == 0);
    CHECK(
        thingsos_book_manage(r, root, "/sdcard/books/manage.txt", "uncommitted.txt") ==
            THINGSOS_MANAGE_STORE &&
        r->generation == before);
    CHECK(remove_directory(p) == 0);
    managed_file = fopen(original, "rb");
    CHECK(managed_file);
    fclose(managed_file);
    CHECK(
        thingsos_book_manage(r, root, "/sdcard/books/manage.txt", "中文书籍.txt") ==
        THINGSOS_MANAGE_OK);
    id = thingsos_path_id("/sdcard/books/中文书籍.txt");
    managed = thingsos_records_book(r, id, NULL, NULL);
    CHECK(managed && managed->offset == 77 && r->marks[0].book_id == id);
    CHECK(
        thingsos_store_load(restored, root) == THINGSOS_STORE_OK &&
        thingsos_book_time(restored, id, 20261012, 20261012) == 33000);
    CHECK(thingsos_book_manage(r, root, "/sdcard/books/中文书籍.txt", NULL) == THINGSOS_MANAGE_OK);
    CHECK(
        managed->order == 0 && r->mark_count == 0 && r->total_ms == 33000 &&
        thingsos_book_time(r, id, 0, 0) == 33000);
    CHECK(thingsos_book_manage(r, root, "/sdcard/books/中文书籍.txt", NULL) == THINGSOS_MANAGE_IO);
    CHECK(!thingsos_book_name(" .txt") && !thingsos_book_name("bad?.txt"));
    CHECK(thingsos_remote_directory("/sdcard") && thingsos_remote_directory("/sdcard/books/相册"));
    const char* protected[]={"/sdcard/.thingsos/config.txt","/sdcard/books/../secret.txt","/sdcard/whiteos/data.txt","/sdcard/System Volume Information/test.txt","/sdcard/books/.hidden.png","/flash/test.txt","/sdcard/firmware.bin","/sdcard/fonts/font.ttf","/sdcard/books/config/cover.png","/sdcard/CONFIG/data.txt","/sdcard/system/status.txt"};
    for(size_t i=0;i<sizeof(protected)/sizeof(protected[0]);++i)
        CHECK(!thingsos_managed_path(protected[i]) && thingsos_book_manage(r,root,protected[i],NULL)==THINGSOS_MANAGE_INVALID);
    CHECK(!thingsos_remote_directory("/sdcard/books/config.") &&
        !thingsos_remote_directory("/sdcard/books/config ") &&
        !thingsos_remote_directory("/sdcard/.. /books"));
    CHECK(thingsos_managed_name("风景.PNG") && !thingsos_managed_name("config.json"));
    /* Upload names: a sender's ellipsis is a plain character run, not a path. */
    CHECK(thingsos_upload_name("image_蓝金色童话原创壁纸系列世界地标建..._0.jpg"));
    CHECK(thingsos_upload_name("a..b.epub") && thingsos_upload_name("第1卷 上.txt"));
    CHECK(!thingsos_upload_name("..") && !thingsos_upload_name(".hidden.jpg") && !thingsos_upload_name(" lead.jpg"));
    CHECK(!thingsos_upload_name("trail.jpg.") && !thingsos_upload_name("trail.jpg "));
    CHECK(!thingsos_upload_name("a/b.jpg") && !thingsos_upload_name("a\\b.jpg") && !thingsos_upload_name("c:d.jpg"));
    CHECK(!thingsos_upload_name("q?.jpg") && !thingsos_upload_name("s*.jpg") && !thingsos_upload_name("p|.jpg") &&
          !thingsos_upload_name("x<y>.jpg") && !thingsos_upload_name("say\"hi\".jpg"));
    CHECK(!thingsos_upload_name("tab\t.jpg") && !thingsos_upload_name(""));
    {
        char longest[192];memset(longest,'a',186);memcpy(longest+186,".jpg",5);
        CHECK(thingsos_upload_name(longest)); /* 190 bytes */
        char over[193];memset(over,'a',187);memcpy(over+187,".jpg",5);
        CHECK(!thingsos_upload_name(over));   /* 191 bytes */
    }
    /* An accepted upload name is still manageable as a file path. */
    CHECK(thingsos_managed_path("/sdcard/wallpapers/image_蓝金色童话原创壁纸系列世界地标建..._0.jpg"));
    snprintf(p,sizeof(p),"%s/books/remote-image.png",root);managed_file=fopen(p,"wb");CHECK(managed_file);fputs("image",managed_file);fclose(managed_file);
    size_t image_record_count=r->book_count;
    CHECK(thingsos_book_manage(r,root,"/sdcard/books/remote-image.png","different.jpg")==THINGSOS_MANAGE_INVALID);
    CHECK(thingsos_book_manage(r,root,"/sdcard/books/remote-image.png","重命名.png")==THINGSOS_MANAGE_OK);
    CHECK(r->book_count==image_record_count);
    CHECK(thingsos_book_manage(r,root,"/sdcard/books/重命名.png",NULL)==THINGSOS_MANAGE_OK);
    /* Audio has no book record, so a delete only removes the directory entry:
       both supported formats work with case-insensitive suffixes and the
       records store is not rewritten. */
    snprintf(p,sizeof(p),"%s/music",root);make_directory(p);
    const char* audio_names[]={"delete-test.MP3","delete-test.WAV"};
    size_t audio_record_count=r->book_count;
    for(size_t a=0;a<2;++a){
        char logical[512];snprintf(logical,sizeof(logical),"/sdcard/music/%s",audio_names[a]);
        CHECK(thingsos_managed_name(audio_names[a]) && thingsos_managed_path(logical));
        snprintf(p,sizeof(p),"%s/music/%s",root,audio_names[a]);
        managed_file=fopen(p,"wb");CHECK(managed_file);fputs("audio fixture",managed_file);fclose(managed_file);
        CHECK(thingsos_book_manage(r,root,logical,NULL)==THINGSOS_MANAGE_OK);
        managed_file=fopen(p,"rb");CHECK(!managed_file && r->book_count==audio_record_count);
    }
    CHECK(!thingsos_managed_path("/sdcard/.thingsos/private.mp3") &&
          !thingsos_managed_path("/sdcard/music/../system/private.wav"));
    uint64_t audio_generation=r->generation;
    snprintf(p,sizeof(p),"%s/music/no-record.mp3",root);
    managed_file=fopen(p,"wb");CHECK(managed_file);fputs("audio fixture",managed_file);fclose(managed_file);
    CHECK(thingsos_book_manage(r,root,"/sdcard/music/no-record.mp3",NULL)==THINGSOS_MANAGE_OK);
    managed_file=fopen(p,"rb");
    CHECK(!managed_file && r->generation==audio_generation && r->book_count==audio_record_count);
#ifdef _WIN32
    /* A directory entry that cannot be removed must report a failure instead
       of the silent success the staged transaction used to return. */
    snprintf(p,sizeof(p),"%s/music/locked.mp3",root);
    managed_file=fopen(p,"wb");CHECK(managed_file);fputs("locked",managed_file);fclose(managed_file);
    FILE* locked=fopen(p,"rb");CHECK(locked);
    CHECK(thingsos_book_manage(r,root,"/sdcard/music/locked.mp3",NULL)==THINGSOS_MANAGE_IO);
    fclose(locked);
    CHECK(thingsos_book_manage(r,root,"/sdcard/music/locked.mp3",NULL)==THINGSOS_MANAGE_OK);
    managed_file=fopen(p,"rb");CHECK(!managed_file);
#endif
    snprintf(original, sizeof(original), "%s/books/power.txt", root);
    managed_file = fopen(original, "wb");
    CHECK(managed_file);
    fputs("power failure", managed_file);
    fclose(managed_file);
    snprintf(p, sizeof(p), "%s/books/power-future.txt", root);
    CHECK(rename(original, p) == 0);
    interrupted_journal(root, r, "/sdcard/books/power.txt", "/books/power-future.txt", false);
    CHECK(thingsos_manage_recover(r, root));
    managed_file = fopen(original, "rb");
    CHECK(managed_file);
    fclose(managed_file);
    CHECK(remove(original) == 0);
    char staged[512];
    snprintf(
        staged, sizeof(staged), "/books/.deleted-%016llx-%llu.tmp",
        (unsigned long long)thingsos_path_id("/sdcard/books/power.txt"),
        (unsigned long long)(r->generation - 1));
    snprintf(p, sizeof(p), "%s%s", root, staged);
    managed_file = fopen(p, "wb");
    CHECK(managed_file);
    fputs("committed delete", managed_file);
    fclose(managed_file);
    interrupted_journal(root, r, "/sdcard/books/power.txt", staged, true);
    CHECK(thingsos_manage_recover(r, root));
    CHECK(fopen(p, "rb") == NULL);
#ifdef _WIN32
    /* A staged delete whose unlink fails restores the visible name. */
    snprintf(original, sizeof(original), "%s/books/readonly.txt", root);
    managed_file = fopen(original, "wb");
    CHECK(managed_file);
    fputs("read only", managed_file);
    fclose(managed_file);
    CHECK(thingsos_records_book(
        r, thingsos_path_id("/sdcard/books/readonly.txt"), "/sdcard/books/readonly.txt",
        "readonly.txt"));
    CHECK(thingsos_store_save_compatible(r, root));
    CHECK(chmod(original, 0444) == 0);
    CHECK(
        thingsos_book_manage(r, root, "/sdcard/books/readonly.txt", NULL) ==
        THINGSOS_MANAGE_IO);
    CHECK(chmod(original, 0666) == 0);
    managed_file = fopen(original, "rb");
    CHECK(managed_file);
    fclose(managed_file);
    CHECK(remove(original) == 0);
#endif
    char alternate[192];
    CHECK(
        thingsos_collision_name("中文书籍.txt", 1, alternate) &&
        !strcmp(alternate, "中文书籍 (1).txt"));
    CHECK(!thingsos_collision_name("../bad.txt", 1, alternate));
    /* Real Pinyin candidates, paging, cursor insertion and byte-limit failure. */
    CHECK(thingsos_input_open(&input, ".txt", 96, false));
    thingsos_input_cursor(&input, 0);
    input.chinese = true;
    CHECK(
        thingsos_ime_letter(&input, 'z') && thingsos_ime_letter(&input, 'h') &&
        thingsos_ime_letter(&input, 'o') && thingsos_ime_letter(&input, 'n') &&
        thingsos_ime_letter(&input, 'g'));
    CHECK(thingsos_ime_count(&input) > 6);
    char candidate[THINGSOS_IME_CANDIDATE_BYTES];
    CHECK(thingsos_ime_candidate(&input, 0, candidate) && !strcmp(candidate, "中"));
    thingsos_ime_page(&input, 1);
    CHECK(input.candidate_page == 1);
    thingsos_ime_page(&input, -1);
    CHECK(thingsos_ime_commit(&input, 0) && !strcmp(input.text, "中.txt"));
    CHECK(
        thingsos_ime_letter(&input, 'l') && thingsos_ime_letter(&input, 'v') &&
        thingsos_ime_count(&input) > 0);
    thingsos_ime_backspace(&input);
    CHECK(!strcmp(input.composition, "l"));
    thingsos_input_clear(&input);
    CHECK(!input.length && !input.composition[0]);
    CHECK(thingsos_input_open(&input, "x", 2, false));
    CHECK(thingsos_ime_letter(&input, 'n') && thingsos_ime_letter(&input, 'i'));
    CHECK(
        !thingsos_ime_commit(&input, 0) && !strcmp(input.text, "x") &&
        !strcmp(input.composition, "ni"));
    free(browser);
    free(wire);
    free(r);
    free(restored);
    puts(
        "PASS: UTF-8 keyboard editing/masking; SD snapshots restart/CRC/torn-write recovery; "
        "dates/stats/bookmarks; directory bounds and navigation");
    return 0;
}
