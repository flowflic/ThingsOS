/* SPDX-License-Identifier: Apache-2.0
 * A worker may be queued for this mutex. Never join while holding it, and
 * never free the shared state before both tasks acknowledge their exit.
 */
static void prefetch_join(prefetch_t* p) {
    atomic_store(&p->stop,true);
    bool owned=thingsos_io_lock && xSemaphoreGetMutexHolder(thingsos_io_lock)==xTaskGetCurrentTaskHandle();
    if(owned)xSemaphoreGive(thingsos_io_lock);
    for(unsigned i=0;i<2;++i)xSemaphoreTake(p->done,portMAX_DELAY);
    if(owned)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
}
