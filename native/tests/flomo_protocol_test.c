// SPDX-License-Identifier: Apache-2.0
#include "thingsos/flomo_protocol.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)

static cJSON* parse_ok(const char* body) {
    static char json[8192];
    CHECK(thingsos_flomo_unwrap_sse(body, strlen(body), json, sizeof(json)));
    void* result = NULL;
    char msg[THINGSOS_FLOMO_MESSAGE_LEN];
    CHECK(thingsos_flomo_parse_result(json, strlen(json), &result, msg, sizeof(msg)));
    return (cJSON*)result;
}

static void flomo_text_rules(void) {
    char out[256];
    /* Markdown escapes keep the character; list paragraphs leave no blank line. */
    thingsos_flomo_plain_text("fmcp\\_QK\\*x", strlen("fmcp\\_QK\\*x"), out, sizeof(out));
    CHECK(!strcmp(out, "fmcp_QK*x"));
    const char* list = "<ul><li><p>一；</p></li><li><p>二</p></li></ul>";
    thingsos_flomo_plain_text(list, strlen(list), out, sizeof(out));
    CHECK(!strcmp(out, "- 一；\n- 二"));
}
int main(int argc, char** argv) {
    flomo_text_rules();
    char token[THINGSOS_FLOMO_TOKEN_LEN];
    /* Token input: bare, Bearer prefix, pasted Codex/VS Code configuration. */
    CHECK(thingsos_flomo_parse_token("  fl_abcDEF123456  ", token, sizeof(token)) && !strcmp(token, "fl_abcDEF123456"));
    CHECK(thingsos_flomo_parse_token("Bearer fl_abcDEF123456", token, sizeof(token)) && !strcmp(token, "fl_abcDEF123456"));
    CHECK(thingsos_flomo_parse_token("http_headers = { Authorization = \"Bearer fl_abcDEF123456\" }", token, sizeof(token)) &&
          !strcmp(token, "fl_abcDEF123456"));
    CHECK(thingsos_flomo_parse_token("{\"headers\":{\"Authorization\":\"Bearer fl_abc.DEF-123\"}}", token, sizeof(token)) &&
          !strcmp(token, "fl_abc.DEF-123"));
    CHECK(!thingsos_flomo_parse_token("short", token, sizeof(token)));
    CHECK(!thingsos_flomo_parse_token("two words token", token, sizeof(token)));
    CHECK(!thingsos_flomo_parse_token("", token, sizeof(token)));

    CHECK(thingsos_flomo_id_valid("MTAxNjEyMzQ1"));
    CHECK(!thingsos_flomo_id_valid("../etc") && !thingsos_flomo_id_valid("") && !thingsos_flomo_id_valid("a b"));

    /* Requests are JSON-RPC tools/call with escaped arguments. */
    char body[1024];
    CHECK(thingsos_flomo_build_search(7, "读书 \"心理\"", "#读书/心理学", "2026-10-21", 99, body, sizeof(body)));
    cJSON* req = cJSON_Parse(body);
    CHECK(req && !strcmp(cJSON_GetObjectItem(req, "method")->valuestring, "tools/call"));
    cJSON* params = cJSON_GetObjectItem(req, "params");
    CHECK(!strcmp(cJSON_GetObjectItem(params, "name")->valuestring, "memo_search"));
    cJSON* args = cJSON_GetObjectItem(params, "arguments");
    CHECK(!strcmp(cJSON_GetObjectItem(args, "keywords")->valuestring, "读书 \"心理\""));
    CHECK(!strcmp(cJSON_GetObjectItem(args, "tag")->valuestring, "读书/心理学"));
    CHECK(!strcmp(cJSON_GetObjectItem(args, "end_date")->valuestring, "2026-10-21"));
    CHECK(cJSON_GetObjectItem(args, "limit")->valueint == 50);
    cJSON_Delete(req);
    CHECK(thingsos_flomo_build_search(1, "", "", "", 20, body, sizeof(body)) && !strstr(body, "keywords") && !strstr(body, "tag\""));
    CHECK(thingsos_flomo_build_batch_get(2, "MTAxNjEyMzQ1", body, sizeof(body)) && strstr(body, "memo_batch_get"));
    CHECK(!thingsos_flomo_build_batch_get(2, "bad id", body, sizeof(body)));
    CHECK(thingsos_flomo_build_recommended(3, "MTAxNjEyMzQ1", 5, body, sizeof(body)) && strstr(body, "memo_recommended"));
    CHECK(thingsos_flomo_build_tag_tree(4, 300, body, sizeof(body)) && strstr(body, "tag_tree"));
    CHECK(thingsos_flomo_build_daily_review(5, body, sizeof(body)) && strstr(body, "get_daily_review"));

    /* SSE reply with structuredContent, as returned by flomoapp.com/mcp. */
    const char* sse =
        "event: message\r\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"{}\"}],"
        "\"structuredContent\":{\"memos\":["
        "{\"content\":\"<p>今天读完 <mark>重点</mark> 和 **加粗** &amp; 列表</p><ul><li>一</li><li>二</li></ul>\","
        "\"content_truncated\":false,\"created_at\":\"2026-11-17T18:45:47+08:00\",\"files\":[],\"from\":\"human\","
        "\"has_image\":true,\"has_link\":false,\"has_voice\":false,\"id\":\"MTAxNjEyMzQ1\",\"linked_memos\":[],"
        "\"tags\":[\"读书\",\"读书/心理学\"],\"updated_at\":\"2026-11-17T18:45:48+08:00\",\"url\":\"https://v.flomoapp.com/mine/?memo_id=x\",\"word_count\":17},"
        "{\"content\":\"bad\",\"id\":\"../x\"},"
        "{\"content\":\"second\",\"id\":\"MTAxNjEyMzQ2\",\"created_at\":\"2026-03-01T08:00:00+08:00\",\"tags\":[]}"
        "]}}}\r\n\r\n";
    cJSON* result = parse_ok(sse);
    thingsos_flomo_memo_t memos[4];
    unsigned n = thingsos_flomo_read_memos(result, memos, 4);
    CHECK(n == 2);
    CHECK(!strcmp(memos[0].id, "MTAxNjEyMzQ1") && !strcmp(memos[0].tags, "#读书 #读书/心理学"));
    CHECK(!strcmp(memos[0].created_at, "2026-11-17T18:45:47+08:00") && memos[0].word_count == 17);
    CHECK(memos[0].has_image && !memos[0].truncated);
    CHECK(!strcmp(memos[0].preview, "今天读完 重点 和 加粗 & 列表\n- 一\n- 二"));
    CHECK(!strcmp(memos[1].preview, "second") && !memos[1].tags[0]);
    char full[64];
    size_t full_len = 0;
    thingsos_flomo_memo_t one;
    CHECK(thingsos_flomo_read_full(result, "MTAxNjEyMzQ2", &one, full, sizeof(full), &full_len));
    CHECK(full_len == 6 && !strcmp(full, "second") && !strcmp(one.id, "MTAxNjEyMzQ2"));
    CHECK(!thingsos_flomo_read_full(result, "MISSING12345", &one, full, sizeof(full), &full_len));
    cJSON_Delete(result);

    /* Fallback: no structuredContent, JSON in content[0].text; plain JSON body. */
    result = parse_ok("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"tags\\\":[\\\"a\\\",\\\"b/c\\\"],\\\"total\\\":2}\"}]}}");
    thingsos_flomo_tag_t tags[4];
    CHECK(thingsos_flomo_read_tags(result, tags, 4) == 2 && !strcmp(tags[1].name, "b/c"));
    cJSON_Delete(result);

    /* Multi-line SSE data and the last JSON event win. */
    char json[512];
    const char* multi = "event: ping\ndata: hello\n\nevent: message\ndata: {\"jsonrpc\":\"2.0\",\ndata: \"id\":1,\"result\":{}}\n";
    CHECK(thingsos_flomo_unwrap_sse(multi, strlen(multi), json, sizeof(json)) && !strcmp(json, "{\"jsonrpc\":\"2.0\",\n\"id\":1,\"result\":{}}"));
    CHECK(!thingsos_flomo_unwrap_sse("data: nope\n\n", 12, json, sizeof(json)));

    /* Errors: JSON-RPC error and tool isError. */
    void* r = NULL;
    char msg[THINGSOS_FLOMO_MESSAGE_LEN];
    const char* err = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32602,\"message\":\"invalid params\"}}";
    CHECK(!thingsos_flomo_parse_result(err, strlen(err), &r, msg, sizeof(msg)) && !r && !strcmp(msg, "invalid params"));
    const char* tool_err = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"需要 PRO 会员\"}]}}";
    CHECK(!thingsos_flomo_parse_result(tool_err, strlen(tool_err), &r, msg, sizeof(msg)) && !strcmp(msg, "需要 PRO 会员"));
    CHECK(!thingsos_flomo_parse_result("{", 1, &r, msg, sizeof(msg)) && msg[0]);

    /* Plain text never splits UTF-8 at the capacity limit. */
    char small[8];
    size_t len = thingsos_flomo_plain_text("中文测试", strlen("中文测试"), small, sizeof(small));
    CHECK(len == 6 && !strcmp(small, "中文"));
    char text[64];
    CHECK(thingsos_flomo_plain_text("a<br>b\r\n\n\n\nc &lt;x&gt; <u>u</u>", 32, text, sizeof(text)) && !strcmp(text, "a\nb\n\nc <x> u"));

    /* Pagination date: one calendar day earlier, across month/year/leap day. */
    char day[16];
    CHECK(thingsos_flomo_day_before("2026-11-17T18:45:47+08:00", day, sizeof(day)) && !strcmp(day, "2026-11-16"));
    CHECK(thingsos_flomo_day_before("2026-03-01T00:00:00+08:00", day, sizeof(day)) && !strcmp(day, "2026-02-28"));
    CHECK(thingsos_flomo_day_before("2024-03-01", day, sizeof(day)) && !strcmp(day, "2024-02-29"));
    CHECK(thingsos_flomo_day_before("2026-01-01", day, sizeof(day)) && !strcmp(day, "2025-12-31"));
    CHECK(!thingsos_flomo_day_before("bad", day, sizeof(day)));

    /* SD list cache round trip and corruption rejection. */
    char path[512];
    snprintf(path, sizeof(path), "%s/flomo_list_test.dat", argc > 1 ? argv[1] : ".");
    CHECK(thingsos_flomo_save_memos(path, memos, 2));
    thingsos_flomo_memo_t loaded[4];
    unsigned count = 0;
    CHECK(thingsos_flomo_load_memos(path, loaded, 4, &count) && count == 2 && !memcmp(loaded, memos, sizeof(memos[0]) * 2));
    CHECK(!thingsos_flomo_load_memos(path, loaded, 1, &count));
    FILE* f = fopen(path, "r+b");
    CHECK(f && fseek(f, 40, SEEK_SET) == 0 && fputc('X', f) != EOF && fclose(f) == 0);
    CHECK(!thingsos_flomo_load_memos(path, loaded, 4, &count) && count == 0);
    remove(path);

    puts("flomo protocol: token, requests, SSE, memos, tags, errors, text, dates, cache passed");
    return 0;
}
