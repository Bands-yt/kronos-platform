/* Sample plugin for the Kronos plugin API (engine/src/plugin/kronos_plugin.h).
 *
 * - A "Grid Snap" editor panel that snaps every object in the scene to a grid.
 * - An importer that turns PGM heightmaps (.pgm, the format most image
 *   editors can export as "portable graymap") into OBJ terrain meshes.
 *
 * Plain C, so it builds with any compiler and keeps working across engine
 * updates. See docs/PLUGIN_API.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin/kronos_plugin.h"

static const KronosHostApi* api;
static float gridSize = 1.0f;
static int32_t snapHeight = 0;
static char status[128] = "";

static float snap(float value, float grid) {
    float cells = value / grid;
    float rounded = (float)(long)(cells + (cells >= 0 ? 0.5f : -0.5f));
    return rounded * grid;
}

static void snapAll(void) {
    uint32_t count = api->entity_count(api->host);
    uint32_t moved = 0;
    for (uint32_t i = 0; i < count; ++i) {
        KronosEntity entity = api->entity_at(api->host, i);
        float p[3];
        if (api->get_position(api->host, entity, p) != KRONOS_OK) continue;
        float s[3] = {snap(p[0], gridSize), snapHeight ? snap(p[1], gridSize) : p[1], snap(p[2], gridSize)};
        if (s[0] == p[0] && s[1] == p[1] && s[2] == p[2]) continue;
        if (api->set_position(api->host, entity, s) == KRONOS_OK) ++moved;
    }
    snprintf(status, sizeof(status), "Snapped %u of %u objects.", moved, count);
}

static void drawGridSnap(void* user, const KronosUi* ui) {
    char line[64];
    (void)user;
    snprintf(line, sizeof(line), "%u objects in the scene", api->entity_count(api->host));
    ui->text(ui->context, line);
    ui->slider_float(ui->context, "Grid size", &gridSize, 0.25f, 8.0f);
    if (gridSize < 0.01f) gridSize = 0.01f;
    ui->checkbox(ui->context, "Snap height too", &snapHeight);
    if (ui->button(ui->context, "Snap everything")) snapAll();
    if (status[0]) {
        ui->separator(ui->context);
        ui->text(ui->context, status);
    }
}

/* --- PGM heightmap -> OBJ ------------------------------------------------ */

typedef struct Cursor {
    const uint8_t* data;
    uint64_t size;
    uint64_t at;
} Cursor;

static void skipSpace(Cursor* c) {
    while (c->at < c->size) {
        uint8_t ch = c->data[c->at];
        if (ch == '#') {
            while (c->at < c->size && c->data[c->at] != '\n') ++c->at;
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++c->at;
        } else {
            break;
        }
    }
}

static int readNumber(Cursor* c, long* out) {
    long value = 0;
    int digits = 0;
    skipSpace(c);
    while (c->at < c->size && c->data[c->at] >= '0' && c->data[c->at] <= '9' && digits < 9) {
        value = value * 10 + (c->data[c->at++] - '0');
        ++digits;
    }
    *out = value;
    return digits > 0;
}

static int emit(const KronosAssetOutput* out, const char* text) {
    return out->write(out->context, text, strlen(text)) == KRONOS_OK;
}

static int32_t importHeightmap(void* user, const char* name, const uint8_t* data, uint64_t size,
                               const KronosAssetOutput* out) {
    Cursor c = {data, size, 2};
    long width, height, maxValue;
    char line[160];
    (void)user;
    (void)name;
    if (size < 2 || data[0] != 'P' || (data[1] != '2' && data[1] != '5')) return KRONOS_INVALID;
    int binary = data[1] == '5';
    if (!readNumber(&c, &width) || !readNumber(&c, &height) || !readNumber(&c, &maxValue)) return KRONOS_INVALID;
    if (width < 2 || height < 2 || width > 2048 || height > 2048 || maxValue < 1 || maxValue > 65535) {
        return KRONOS_INVALID;
    }
    if (binary) ++c.at;
    int wide = maxValue > 255;
    if (binary && size - c.at < (uint64_t)(width * height * (wide ? 2 : 1))) return KRONOS_INVALID;

    const float spacing = 1.0f;
    const float tallest = 10.0f;
    if (!emit(out, "# Terrain made from a heightmap by the Kronos sample plugin\no Terrain\n")) return KRONOS_ERROR;
    for (long z = 0; z < height; ++z) {
        for (long x = 0; x < width; ++x) {
            long value = 0;
            if (binary) {
                value = wide ? (data[c.at] << 8 | data[c.at + 1]) : data[c.at];
                c.at += wide ? 2 : 1;
            } else if (!readNumber(&c, &value)) {
                return KRONOS_INVALID;
            }
            float y = (float)value / (float)maxValue * tallest;
            snprintf(line, sizeof(line), "v %.3f %.3f %.3f\n", (x - (width - 1) * 0.5f) * spacing, y,
                     (z - (height - 1) * 0.5f) * spacing);
            if (!emit(out, line)) return KRONOS_ERROR;
        }
    }
    for (long z = 0; z + 1 < height; ++z) {
        for (long x = 0; x + 1 < width; ++x) {
            long a = z * width + x + 1, b = a + 1, d = a + width, e = d + 1;
            snprintf(line, sizeof(line), "f %ld %ld %ld\nf %ld %ld %ld\n", a, d, b, b, d, e);
            if (!emit(out, line)) return KRONOS_ERROR;
        }
    }
    return KRONOS_OK;
}

/* --- Entry points ---------------------------------------------------------- */

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT int32_t kronos_plugin_query(KronosPluginInfo* info) {
    info->api_major = KRONOS_PLUGIN_API_MAJOR;
    info->api_minor = KRONOS_PLUGIN_API_MINOR;
    info->id = "kronos.sample.grid-and-terrain";
    info->name = "Grid Snap and Heightmaps";
    info->version = "1.0.0";
    info->author = "Kronos";
    info->capabilities = KRONOS_CAP_SCENE_READ | KRONOS_CAP_SCENE_WRITE | KRONOS_CAP_EDITOR_PANELS |
                         KRONOS_CAP_ASSET_IMPORTERS;
    return KRONOS_OK;
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT int32_t kronos_plugin_load(const KronosHostApi* host, void** instance) {
    KronosPanel panel;
    KronosAssetImporter importer;
    api = host;
    *instance = NULL;

    memset(&panel, 0, sizeof(panel));
    panel.struct_size = sizeof(panel);
    panel.id = "kronos.sample.grid-snap";
    panel.title = "Grid Snap";
    panel.version = 1;
    panel.draw = drawGridSnap;
    api->register_panel(api->host, &panel);

    memset(&importer, 0, sizeof(importer));
    importer.struct_size = sizeof(importer);
    importer.type = "kronos.sample.heightmap";
    importer.version = 1;
    importer.extensions = ".pgm";
    importer.output_extension = ".obj";
    importer.import = importHeightmap;
    api->register_asset_importer(api->host, &importer);
    return KRONOS_OK;
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT void kronos_plugin_unload(void* instance) { (void)instance; }
