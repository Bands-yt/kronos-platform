/* Test plugin for the Kronos plugin API, built as C in several variants. */
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "plugin/kronos_plugin.h"

#ifndef PLUGIN_ID
#define PLUGIN_ID "test.plugin"
#endif
#ifndef PLUGIN_API_MAJOR
#define PLUGIN_API_MAJOR KRONOS_PLUGIN_API_MAJOR
#endif
#ifndef PLUGIN_CAPS
#define PLUGIN_CAPS                                                                                       \
    (KRONOS_CAP_SCENE_READ | KRONOS_CAP_SCENE_WRITE | KRONOS_CAP_ASSET_IMPORTERS | KRONOS_CAP_EDITOR_PANELS | \
     KRONOS_CAP_CHANNELS)
#endif
#ifndef IMPORTER_VERSION
#define IMPORTER_VERSION 1
#endif
#ifndef OUTPUT_PREFIX
#define OUTPUT_PREFIX ""
#endif

static const KronosHostApi* api;
static KronosChannel counter;
static int bumps;
static int32_t enabled = 1;
static float speed = 1.0f;
static char name[32] = "box";

#if defined(__linux__)
static void probe(const uint8_t* data, uint64_t size, char* report, size_t capacity) {
    char writePath[512] = {0};
    char readPath[512] = {0};
    const char* newline = memchr(data, '\n', size);
    if (newline) {
        size_t first = (size_t)(newline - (const char*)data);
        if (first < sizeof(writePath) && size - first - 1 < sizeof(readPath)) {
            memcpy(writePath, data, first);
            memcpy(readPath, newline + 1, size - first - 1);
        }
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    const char* socketResult = s >= 0 ? "ok" : "denied";
    if (s >= 0) close(s);
    pid_t child = fork();
    if (child == 0) _exit(0);
    if (child > 0) waitpid(child, NULL, 0);
    const char* forkResult = child >= 0 ? "ok" : "denied";
    int w = open(writePath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const char* writeResult = w >= 0 ? "ok" : "denied";
    if (w >= 0) close(w);
    int r = open(readPath, O_RDONLY);
    const char* readResult = r >= 0 ? "ok" : "denied";
    if (r >= 0) close(r);
    const char* signalResult = kill(getppid(), 0) == 0 ? "ok" : "denied";
    snprintf(report, capacity, "socket:%s fork:%s write:%s read:%s signal:%s", socketResult, forkResult, writeResult,
             readResult, signalResult);
}
#endif

static int32_t importUpper(void* user, const char* sourceName, const uint8_t* data, uint64_t size,
                           const KronosAssetOutput* output) {
    (void)user;
    if (strcmp(sourceName, "crash.lower") == 0) {
        volatile int* nowhere = NULL;
        *nowhere = 1;
    }
    if (strcmp(sourceName, "hang.lower") == 0) {
        for (volatile int spin = 0;; spin = spin + 1) {
        }
    }
    if (strcmp(sourceName, "fail.lower") == 0) return KRONOS_ERROR;
#if defined(__linux__)
    if (strcmp(sourceName, "probe.lower") == 0) {
        char report[256];
        probe(data, size, report, sizeof(report));
        return output->write(output->context, report, strlen(report));
    }
#endif
    output->write(output->context, OUTPUT_PREFIX, strlen(OUTPUT_PREFIX));
    for (uint64_t i = 0; i < size; ++i) {
        uint8_t c = data[i];
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
        if (output->write(output->context, &c, 1) != KRONOS_OK) return KRONOS_ERROR;
    }
    return KRONOS_OK;
}

static void drawPanel(void* user, const KronosUi* ui) {
    char line[96];
    (void)user;
    ui->text(ui->context, "hello from " PLUGIN_ID);
    if (ui->button(ui->context, "Bump")) ++bumps;
    ui->same_line(ui->context);
    ui->checkbox(ui->context, "Enabled", &enabled);
    ui->slider_float(ui->context, "Speed", &speed, 0.0f, 10.0f);
    ui->input_text(ui->context, "Name", name, sizeof(name));
    ui->separator(ui->context);
    snprintf(line, sizeof(line), "bumps=%d enabled=%d speed=%.1f name=%s", bumps, (int)enabled, (double)speed, name);
    ui->text(ui->context, line);
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT int32_t kronos_plugin_query(KronosPluginInfo* info) {
    info->api_major = PLUGIN_API_MAJOR;
    info->api_minor = KRONOS_PLUGIN_API_MINOR;
    info->id = PLUGIN_ID;
    info->name = "Test Plugin";
    info->version = "1.0.0";
    info->author = "Kronos tests";
    info->capabilities = PLUGIN_CAPS;
    return KRONOS_OK;
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT int32_t kronos_plugin_load(const KronosHostApi* host, void** instance) {
    KronosAssetImporter importer;
    KronosPanel panel;
    char message[128];
    api = host;
    *instance = &bumps;

    memset(&importer, 0, sizeof(importer));
    importer.struct_size = sizeof(importer);
    importer.type = "test.upper";
    importer.version = IMPORTER_VERSION;
    importer.extensions = ".lower; LOW";
    importer.output_extension = ".txt";
    importer.import = importUpper;
    int32_t importerResult = api->register_asset_importer(api->host, &importer);

    memset(&panel, 0, sizeof(panel));
    panel.struct_size = sizeof(panel);
    panel.id = "test.panel";
    panel.title = "Test Panel";
    panel.version = IMPORTER_VERSION;
    panel.draw = drawPanel;
    int32_t panelResult = api->register_panel(api->host, &panel);

    int32_t channelResult = api->open_channel(api->host, "test.counter", 1, 64, &counter);
    snprintf(message, sizeof(message), "loaded importer=%d panel=%d channel=%d sandboxed=%d", (int)importerResult,
             (int)panelResult, (int)channelResult, (int)api->sandboxed);
    api->log(api->host, KRONOS_LOG_INFO, message);
    return KRONOS_OK;
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT void kronos_plugin_tick(void* instance, float dt) {
    (void)instance;
    (void)dt;
    if (counter.data) ++*(uint64_t*)counter.data;
    KronosEntity box = api->find_entity(api->host, "Box");
    float position[3] = {0, 0, 0};
    if (api->get_position(api->host, box, position) == KRONOS_OK) {
        position[0] += 1.0f;
        api->set_position(api->host, box, position);
    }
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT void kronos_plugin_unload(void* instance) {
    (void)instance;
    api->log(api->host, KRONOS_LOG_INFO, "unloaded");
    counter.data = NULL;
}
