/* SPDX-License-Identifier: Apache-2.0
 * Main-task-only idle title/section discovery, paused while the TOC is shown.
 * Entry centers the current chapter; a visible row keeps its drawn target. */
static size_t reader_toc_current(void) {
    size_t count=reader_toc_count();
    if(!count)return SIZE_MAX;
    if(!app.epub_open) {
        size_t index=current_page()/10;return index<count?index:count-1;
    }
    if(!app.epub.section_toc_count)return app.chapter_index<count?app.chapter_index:count-1;
    /* Match page-start position, including subheadings within one chapter.
       Equal anchors prefer the deepest entry so only one card is selected. */
    size_t best=SIZE_MAX;
    for(size_t i=0;i<count;++i) {
        const thingsos_toc_entry_t* entry=&app.epub.section_toc[i];
        if(entry->chapter>app.chapter_index || (entry->chapter==app.chapter_index&&entry->offset>app.start))continue;
        if(best==SIZE_MAX){best=i;continue;}
        const thingsos_toc_entry_t* prior=&app.epub.section_toc[best];
        if(entry->chapter>prior->chapter || (entry->chapter==prior->chapter&&
            (entry->offset>prior->offset || (entry->offset==prior->offset&&entry->level>=prior->level))))best=i;
    }
    return best;
}
static size_t reader_toc_center_start(void) {
    size_t count=reader_toc_count(),current=reader_toc_current();
    if(count<=6 || current==SIZE_MAX)return 0;
    size_t start=current>3?current-3:0;
    return start<count-6?start:count-6;
}
static bool reader_background_work_allowed(void) {
    return app.page==THINGSOS_UI_READING&&!app.asleep&&!app.menu&&!app.jump&&!app.controls_open&&
        !app.image_view.open&&!app.annotation_open&&!app.annotation_selecting&&!app.wd_notes_open&&
        !app.wd_thought_open&&!app.wd_cloud_prompt&&!app.wd_consent;
}
static void reader_toc_poll(void) {
    /* Discover while the reading surface is idle, never after a TOC is shown.
       Entry still centers the current chapter; no metadata job can reorder
       that visible list and demand a second display update. */
    if(!app.toc_pending || !app.opened || !app.epub_open || !reader_background_work_allowed() ||
       app.dirty || now()<app.activity_at+1500 || thingsos_weidu_busy() || thingsos_network_busy())return;
    if(app.toc_invalidate_pending) {
        free(app.epub.section_toc);app.epub.section_toc=NULL;
        app.epub.section_toc_count=0;app.epub.section_toc_ready=false;app.epub.section_toc_next=0;
        app.toc_invalidate_pending=false;
    }
    bool done=thingsos_epub_titles_step(&app.epub);
    if(done && app.wd_id[0]) {
        if(!thingsos_wd_navigation_step(WD_ROOT,app.wd_id,&app.epub,&done))done=true;
    }
    app.toc_pending=!done;
}
