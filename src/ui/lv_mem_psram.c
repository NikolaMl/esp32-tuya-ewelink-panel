/**
 * LVGL alokator koji sve drži u PSRAM-u (LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM).
 * LVGL objekti su male alokacije koje bi inače išle u interni RAM, a njega
 * čuvamo za Wi-Fi i TLS (svaka HTTPS veza troši ~40 KB internog RAM-a).
 */
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
#include <esp_heap_caps.h>
#include <string.h>

#define LV_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void * lv_malloc_core(size_t size) { return heap_caps_malloc(size, LV_CAPS); }

void * lv_realloc_core(void * p, size_t new_size) { return heap_caps_realloc(p, new_size, LV_CAPS); }

void lv_free_core(void * p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    memset(mon_p, 0, sizeof(*mon_p));
    mon_p->free_size = heap_caps_get_free_size(LV_CAPS);
    mon_p->total_size = heap_caps_get_total_size(LV_CAPS);
    mon_p->free_biggest_size = heap_caps_get_largest_free_block(LV_CAPS);
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

#endif
