/* Resolve call site ids to source locations.
 * addr2line is called once per module with all of its offsets: call sites
 * repeat heavily, so even huge traces have only a few hundred. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "reader.h"

#define CHUNK 128   /* offsets per addr2line invocation */

static void resolve_module(const char* module, uint32_t* ids, uint64_t* offs,
                           uint32_t n, char** out)
{
    for (uint32_t start = 0; start < n; start += CHUNK) {
        uint32_t cnt = (n - start < CHUNK) ? (n - start) : CHUNK;

        size_t cap = strlen(module) + 64 + cnt * 20;
        char* cmd = (char*) malloc(cap);
        int len = snprintf(cmd, cap, "addr2line -C -e '%s'", module);
        for (uint32_t i = 0; i < cnt; i++)
            len += snprintf(cmd + len, cap - len, " 0x%lx",
                            (unsigned long) offs[start + i]);
        snprintf(cmd + len, cap - len, " 2>/dev/null");

        FILE* p = popen(cmd, "r");
        free(cmd);
        if (!p) return;

        char line[1024];
        for (uint32_t i = 0; i < cnt && fgets(line, sizeof(line), p); i++) {
            line[strcspn(line, "\n")] = '\0';
            uint32_t id = ids[start + i];
            if (line[0] && strncmp(line, "??", 2) != 0)
                out[id] = strdup(line);
        }
        pclose(p);
    }
}

CallSiteTable* callsite_table_load(const char* combined_path, int rank)
{
    FILE* f = fopen(combined_path, "rb");
    if (!f) return NULL;

    RecorderFileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 ||
        memcmp(hdr.magic, "RECORDER", 8) != 0) { fclose(f); return NULL; }

    RecorderSectionEntry* tab = (RecorderSectionEntry*)
        malloc(sizeof(RecorderSectionEntry) * hdr.num_sections);
    if (fread(tab, sizeof(RecorderSectionEntry), hdr.num_sections, f) != hdr.num_sections) {
        free(tab); fclose(f); return NULL;
    }

    uint64_t off = 0, size = 0;
    int found = 0;
    for (uint32_t i = 0; i < hdr.num_sections; i++)
        if (tab[i].type == (uint32_t)RECORDER_SECTION_CALLSITES && tab[i].rank == rank) {
            off = tab[i].offset; size = tab[i].size; found = 1; break;
        }
    free(tab);
    if (!found || size < sizeof(CallSiteSectionHeader)) { fclose(f); return NULL; }

    char* sec = (char*) malloc(size);
    fseek(f, (long) off, SEEK_SET);
    if (fread(sec, 1, size, f) != size) { free(sec); fclose(f); return NULL; }
    fclose(f);

    CallSiteSectionHeader* sh = (CallSiteSectionHeader*) sec;
    if (sh->num_sites == 0) { free(sec); return NULL; }

    /* module paths */
    char** mods = (char**) calloc(sh->num_modules, sizeof(char*));
    char* p = sec + sizeof(CallSiteSectionHeader);
    for (uint32_t i = 0; i < sh->num_modules; i++) { mods[i] = p; p += strlen(p) + 1; }

    CallSiteEntry* ent = (CallSiteEntry*) (sec + sizeof(CallSiteSectionHeader)
                                               + sh->strtab_bytes);

    CallSiteTable* t = (CallSiteTable*) calloc(1, sizeof(CallSiteTable));
    t->num_sites = sh->num_sites;
    t->loc = (char**) calloc(sh->num_sites, sizeof(char*));

    /* one addr2line run per module */
    uint32_t* ids  = (uint32_t*) malloc(sizeof(uint32_t) * sh->num_sites);
    uint64_t* offs = (uint64_t*) malloc(sizeof(uint64_t) * sh->num_sites);
    for (uint32_t m = 0; m < sh->num_modules; m++) {
        uint32_t n = 0;
        for (uint32_t i = 0; i < sh->num_sites; i++)
            if (ent[i].module_id == m) { ids[n] = i; offs[n] = ent[i].offset; n++; }
        if (n) resolve_module(mods[m], ids, offs, n, t->loc);
    }
    free(ids); free(offs);

    /* unresolved: keep module+offset, still usable later */
    for (uint32_t i = 0; i < sh->num_sites; i++) {
        if (t->loc[i]) continue;
        const char* mod = (ent[i].module_id < sh->num_modules)
                        ? mods[ent[i].module_id] : "?";
        const char* base = strrchr(mod, '/');
        char buf[512];
        snprintf(buf, sizeof(buf), "%s+0x%lx", base ? base + 1 : mod,
                 (unsigned long) ent[i].offset);
        t->loc[i] = strdup(buf);
    }

    free(mods);
    free(sec);
    return t;
}

const char* callsite_table_get(CallSiteTable* t, uint32_t id)
{
    if (!t || id >= t->num_sites || !t->loc[id]) return NULL;
    return t->loc[id];
}

void callsite_table_free(CallSiteTable* t)
{
    if (!t) return;
    for (uint32_t i = 0; i < t->num_sites; i++) free(t->loc[i]);
    free(t->loc);
    free(t);
}
