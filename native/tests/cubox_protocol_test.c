// SPDX-License-Identifier: Apache-2.0
#include "thingsos/cubox_protocol.h"
#include "thingsos/content.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                         \
    do {                                                                                    \
        if (!(expr)) {                                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                 \
            exit(1);                                                                        \
        }                                                                                   \
    } while (0)

static void test_credentials(void) {
    char server[THINGSOS_CUBOX_SERVER_LEN];
    char token[THINGSOS_CUBOX_TOKEN_LEN];

    CHECK(thingsos_cubox_parse_credentials(
        "  https://cubox.pro/c/api/save/abc12345DEF/?from=share  ", "cubox.cc", server,
        sizeof(server), token, sizeof(token)));
    CHECK(!strcmp(server, "cubox.pro"));
    CHECK(!strcmp(token, "abc12345DEF"));

    CHECK(thingsos_cubox_parse_credentials(
        "https://CUBOX.CC/c/api/save/zz99887766/", NULL, server, sizeof(server), token,
        sizeof(token)));
    CHECK(!strcmp(server, "cubox.cc"));
    CHECK(!strcmp(token, "zz99887766"));

    CHECK(thingsos_cubox_parse_credentials(
        "raw-token-12345678", "cubox.cc", server, sizeof(server), token, sizeof(token)));
    CHECK(!strcmp(server, "cubox.cc"));
    CHECK(!strcmp(token, "raw-token-12345678"));

    CHECK(thingsos_cubox_parse_credentials(
        "cubox.pro/c/api/save/no_scheme_tok123", NULL, server, sizeof(server), token,
        sizeof(token)));
    CHECK(!strcmp(server, "cubox.pro"));
    CHECK(!strcmp(token, "no_scheme_tok123"));

    CHECK(!thingsos_cubox_parse_credentials(
        "http://cubox.pro/c/api/save/abc12345", NULL, server, sizeof(server), token,
        sizeof(token)));
    CHECK(!thingsos_cubox_parse_credentials(
        "https://evil.example/c/api/save/abc12345", NULL, server, sizeof(server), token,
        sizeof(token)));
    CHECK(!thingsos_cubox_parse_credentials(
        "https://cubox.pro/other/abc12345", NULL, server, sizeof(server), token, sizeof(token)));
    CHECK(!thingsos_cubox_parse_credentials("short", "cubox.pro", server, sizeof(server), token, sizeof(token)));
    CHECK(!thingsos_cubox_parse_credentials("bad\ttoken1234", "cubox.pro", server, sizeof(server), token, sizeof(token)));
}

static void test_ids_and_urls(void) {
    CHECK(thingsos_cubox_id_valid("123456_abc-XYZ"));
    CHECK(!thingsos_cubox_id_valid(""));
    CHECK(!thingsos_cubox_id_valid("../secret"));
    CHECK(!thingsos_cubox_id_valid("a/b"));
    CHECK(!thingsos_cubox_id_valid("id with space"));

    CHECK(thingsos_cubox_http_url_valid("https://img.example.com/a/b.jpg?w=800"));
    CHECK(thingsos_cubox_http_url_valid("http://cdn.example.org/x.png"));
    CHECK(!thingsos_cubox_http_url_valid("javascript:alert(1)"));
    CHECK(!thingsos_cubox_http_url_valid("file:///etc/passwd"));
    CHECK(!thingsos_cubox_http_url_valid("/relative/path.png"));
    CHECK(!thingsos_cubox_http_url_valid("https://bad host/a.png"));
}

static void test_request_builders(void) {
    char buf[512];
    CHECK(thingsos_cubox_build_filter_request(
        THINGSOS_CUBOX_FILTER_UNREAD, "fld_1", NULL, "card_9", 1, 20, buf, sizeof(buf)));
    cJSON* j = cJSON_Parse(buf);
    CHECK(j != NULL);
    CHECK(cJSON_IsFalse(cJSON_GetObjectItem(j, "archived")));
    CHECK(cJSON_IsFalse(cJSON_GetObjectItem(j, "read")));
    CHECK(cJSON_GetObjectItem(j, "starred") == NULL);
    CHECK(!strcmp(cJSON_GetObjectItem(j, "last_card_id")->valuestring, "card_9"));
    CHECK(cJSON_GetObjectItem(j, "page") == NULL);
    cJSON_Delete(j);

    CHECK(thingsos_cubox_build_filter_request(
        THINGSOS_CUBOX_FILTER_STARRED, NULL, "e-ink \"quote\"", "ignored", 3, 20, buf, sizeof(buf)));
    j = cJSON_Parse(buf);
    CHECK(j != NULL);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItem(j, "starred")));
    CHECK(!strcmp(cJSON_GetObjectItem(j, "keyword")->valuestring, "e-ink \"quote\""));
    CHECK(cJSON_GetObjectItem(j, "page")->valueint == 3);
    CHECK(cJSON_GetObjectItem(j, "last_card_id") == NULL);
    cJSON_Delete(j);

    CHECK(thingsos_cubox_build_update_request("c_1", 1, -1, buf, sizeof(buf)));
    j = cJSON_Parse(buf);
    CHECK(j && !strcmp(cJSON_GetObjectItem(j, "id")->valuestring, "c_1") &&
          cJSON_IsTrue(cJSON_GetObjectItem(j, "read")) &&
          cJSON_GetObjectItem(j, "starred") == NULL);
    cJSON_Delete(j);

    CHECK(thingsos_cubox_build_archive_request("c_2", buf, sizeof(buf)));
    j = cJSON_Parse(buf);
    CHECK(j && cJSON_GetArraySize(cJSON_GetObjectItem(j, "card_ids")) == 1);
    cJSON_Delete(j);
}

static void test_parsers(void) {
    const char* err_json = "{\"code\":401,\"message\":\"invalid token\",\"data\":[]}";
    thingsos_cubox_folder_t folders[8];
    unsigned n = 99;
    int code = 0;
    char msg[64];
    CHECK(!thingsos_cubox_parse_folders(
        err_json, strlen(err_json), folders, 8, &n, &code, msg, sizeof(msg)));
    CHECK(code == 401 && n == 0 && !strcmp(msg, "invalid token"));

    /* code==0 must NOT be treated as success for Cubox CLI API */
    const char* zero_json = "{\"code\":0,\"message\":\"not 200\",\"data\":[]}";
    CHECK(!thingsos_cubox_parse_folders(
        zero_json, strlen(zero_json), folders, 8, &n, &code, msg, sizeof(msg)));
    CHECK(code == 0);

    const char* fld_json =
        "{\"code\":200,\"message\":\"ok\",\"data\":["
        "{\"id\":\"f1\",\"name\":\"Inbox\",\"nested_name\":\"Inbox\",\"parent_id\":\"\",\"uncategorized\":true},"
        "{\"id\":\"../bad\",\"name\":\"Trap\",\"nested_name\":\"Trap\",\"parent_id\":\"\"},"
        "{\"id\":\"f2\",\"name\":\"Tech\",\"nested_name\":\"Reading / Tech\",\"parent_id\":\"f0\",\"uncategorized\":false}"
        "]}";
    CHECK(thingsos_cubox_parse_folders(
        fld_json, strlen(fld_json), folders, 8, &n, &code, msg, sizeof(msg)));
    CHECK(code == 200 && n == 2);
    CHECK(!strcmp(folders[0].id, "f1") && folders[0].uncategorized);
    CHECK(!strcmp(folders[1].id, "f2") && !strcmp(folders[1].nested_name, "Reading / Tech"));

    const char* cards_json =
        "{\"code\":200,\"message\":\"\",\"data\":["
        "{\"id\":\"c100\",\"title\":\"Hello E-Ink\",\"article_title\":\"Original\",\"description\":\"Summary text\","
        "\"domain\":\"example.com\",\"read\":false,\"starred\":true,\"archived\":false,"
        "\"folder\":{\"id\":\"f2\",\"name\":\"Tech\",\"nested_name\":\"Reading / Tech\"},"
        "\"tags\":[{\"name\":\"eink\"},{\"name\":\"esp32\"}],"
        "\"url\":\"https://example.com/post\",\"create_time\":\"2026-11-15T10:00:00Z\"}"
        "]}";
    thingsos_cubox_card_t cards[4];
    CHECK(thingsos_cubox_parse_cards(
        cards_json, strlen(cards_json), cards, 4, &n, &code, msg, sizeof(msg)));
    CHECK(n == 1 && !strcmp(cards[0].id, "c100") && cards[0].starred && !cards[0].read);
    CHECK(!strcmp(cards[0].folder_id, "f2") && !strcmp(cards[0].folder_name, "Reading / Tech"));
    CHECK(strstr(cards[0].tag_summary, "eink") && strstr(cards[0].tag_summary, "esp32"));

    const char* det_json =
        "{\"code\":200,\"message\":\"\",\"data\":{"
        "\"id\":\"c100\",\"title\":\"Hello E-Ink\",\"domain\":\"example.com\",\"author\":\"Alice\","
        "\"read\":true,\"starred\":false,\"url\":\"https://example.com/post\","
        "\"content\":\"Paragraph one\\n\\n![Chart](https://cdn.example.com/chart(1).png \\\"Fig 1\\\")\","
        "\"insight\":{\"id\":\"i1\",\"summary\":\"Key takeaway\",\"qas\":[{\"q\":\"Why?\",\"a\":\"Because.\"}]},"
        "\"annotations\":[{\"id\":\"a1\",\"text\":\"Important quote\",\"note\":\"Check this\",\"color\":\"yellow\",\"create_time\":\"2026-11-15\"}]"
        "}}";
    thingsos_cubox_detail_t detail;
    char md[1024];
    size_t md_len = 0;
    CHECK(thingsos_cubox_parse_detail(
        det_json, strlen(det_json), &detail, md, sizeof(md), &md_len, &code, msg, sizeof(msg)));
    CHECK(!strcmp(detail.author, "Alice"));
    CHECK(detail.has_insight && !strcmp(detail.summary, "Key takeaway"));
    CHECK(detail.qa_count == 1 && !strcmp(detail.qas[0].q, "Why?"));
    CHECK(detail.annotation_count == 1 && !strcmp(detail.annotations[0].note, "Check this"));
    CHECK(strstr(md, "chart(1).png") != NULL);
}

static void test_images_and_markdown_rewrite(void) {
    const unsigned char jpg[6] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10};
    const unsigned char png[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const unsigned char gif[6] = {'G', 'I', 'F', '8', '9', 'a'};
    const unsigned char webp[12] = {'R', 'I', 'F', 'F', 1, 2, 3, 4, 'W', 'E', 'B', 'P'};
    CHECK(!strcmp(thingsos_cubox_image_ext(jpg, sizeof(jpg)), ".jpg"));
    CHECK(!strcmp(thingsos_cubox_image_ext(png, sizeof(png)), ".png"));
    CHECK(!strcmp(thingsos_cubox_image_ext(gif, sizeof(gif)), ".gif"));
    CHECK(!strcmp(thingsos_cubox_image_ext(webp, sizeof(webp)), ".webp"));
    CHECK(thingsos_cubox_image_ext("<html>not image</html>", 22) == NULL);

    const char* raw_md =
        "Intro text ![first](https://img.example.com/a(1).webp \"cap\") mid ![dup](https://img.example.com/a(1).webp)\n"
        "![fail](https://img.example.com/missing.jpg)\n"
        "![bad](javascript:alert(1))\n"
        "```md\n"
        "![code](https://img.example.com/inside_code.png)\n"
        "```\n";

    thingsos_cubox_image_ref_t refs[16];
    unsigned count = thingsos_cubox_extract_images(raw_md, refs, 16);
    CHECK(count == 2);
    CHECK(!strcmp(refs[0].url, "https://img.example.com/a(1).webp"));
    CHECK(!strcmp(refs[1].url, "https://img.example.com/missing.jpg"));

    refs[0].downloaded = true;
    strcpy(refs[0].filename, "img_00.webp");
    refs[1].downloaded = false;

    thingsos_cubox_detail_t d = {0};
    strcpy(d.card.id, "c200");
    strcpy(d.card.title, "Article Title");
    strcpy(d.card.domain, "example.com");
    strcpy(d.author, "Bob");
    d.has_insight = true;
    strcpy(d.summary, "Short summary");

    char out[4096];
    size_t written = 0;
    CHECK(thingsos_cubox_build_article_markdown(&d, raw_md, refs, count, out, sizeof(out), &written));
    CHECK(strstr(out, "# Article Title") != NULL);
    CHECK(strstr(out, "## AI 解读") != NULL);
    CHECK(strstr(out, "![first](img_00.webp)") != NULL);
    CHECK(strstr(out, "![dup](img_00.webp)") != NULL);
    CHECK(strstr(out, "[图片: fail]") != NULL);
    CHECK(strstr(out, "[图片: bad]") != NULL);
    CHECK(strstr(out, "![code](https://img.example.com/inside_code.png)") != NULL);

    /* Tight cap tests UTF-8 truncation boundary safety */
    char small[300];
    size_t sw = 0;
    CHECK(thingsos_cubox_build_article_markdown(&d, "中文测试段落不断重复中文测试段落", NULL, 0, small, sizeof(small), &sw));
    CHECK(sw < sizeof(small));
    for (size_t i = 0; i < sw; ++i) {
        unsigned char c = (unsigned char)small[i];
        if ((c & 0x80u) == 0) continue;
        unsigned seq = (c >= 0xF0u) ? 4 : (c >= 0xE0u) ? 3 : (c >= 0xC0u) ? 2 : 0;
        CHECK(seq >= 2 && i + seq <= sw);
        for (unsigned j = 1; j < seq; ++j) CHECK(((unsigned char)small[i + j] & 0xC0u) == 0x80u);
        i += seq - 1;
    }
}

static void test_binary_cache(const char* dir) {
    char fp[512], cp[512];
    snprintf(fp, sizeof(fp), "%s/cubox_folders.dat", dir);
    snprintf(cp, sizeof(cp), "%s/cubox_cards.dat", dir);

    thingsos_cubox_folder_t in_f[2] = {0}, out_f[4] = {0};
    strcpy(in_f[0].id, "f1");
    strcpy(in_f[0].name, "Tech");
    strcpy(in_f[1].id, "f2");
    strcpy(in_f[1].name, "News");
    unsigned fc = 0;
    CHECK(thingsos_cubox_save_folders(fp, in_f, 2));
    CHECK(thingsos_cubox_load_folders(fp, out_f, 4, &fc));
    CHECK(fc == 2 && !strcmp(out_f[1].id, "f2"));

    thingsos_cubox_card_t in_c[1] = {0}, out_c[4] = {0};
    strcpy(in_c[0].id, "c999");
    strcpy(in_c[0].title, "Cached card");
    in_c[0].starred = true;
    unsigned cc = 0;
    CHECK(thingsos_cubox_save_cards(cp, in_c, 1));
    CHECK(thingsos_cubox_load_cards(cp, out_c, 4, &cc));
    CHECK(cc == 1 && !strcmp(out_c[0].id, "c999") && out_c[0].starred);

    FILE* f = fopen(cp, "r+b");
    CHECK(f != NULL);
    CHECK(fseek(f, 20, SEEK_SET) == 0);
    fputc('X', f);
    fclose(f);
    CHECK(!thingsos_cubox_load_cards(cp, out_c, 4, &cc));
    remove(fp);
    remove(cp);
}

int main(int argc, char** argv) {
    const char* scratch = argc > 1 ? argv[1] : ".";
    test_credentials();
    test_ids_and_urls();
    test_request_builders();
    test_parsers();
    test_images_and_markdown_rewrite();
    test_binary_cache(scratch);
    puts("PASS: cubox_protocol_test");
    return 0;
}
