/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Faris Nadim */
/*
 * Kronos plugin API: the stable C interface for native plugins.
 *
 * A plugin is a shared library exporting kronos_plugin_query,
 * kronos_plugin_load and kronos_plugin_unload (kronos_plugin_tick is
 * optional). Only C types cross the boundary, so a plugin can be built with
 * any compiler or language that can produce a C shared library, and doesn't
 * need rebuilding when the engine changes.
 *
 * Versioning: a major version bump breaks compatibility and the host refuses
 * plugins built for another major version. Minor versions only append fields
 * to the end of structs. Every struct starts with struct_size, so each side
 * reads only the fields the other side knows about; use KRONOS_HAS_FIELD
 * before touching a field added after 1.0.
 *
 * See docs/PLUGIN_API.md.
 */
#ifndef KRONOS_PLUGIN_H
#define KRONOS_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#define KRONOS_PLUGIN_API_MAJOR 1
#define KRONOS_PLUGIN_API_MINOR 0

#if defined(_WIN32)
#define KRONOS_PLUGIN_EXPORT __declspec(dllexport)
#else
#define KRONOS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#define KRONOS_PLUGIN_EXTERN extern "C"
#else
#define KRONOS_PLUGIN_EXTERN
#endif

#define KRONOS_HAS_FIELD(ptr, type, field) \
    ((ptr)->struct_size >= offsetof(type, field) + sizeof(((type*)0)->field))

/* Results */
#define KRONOS_OK 0
#define KRONOS_ERROR (-1)
#define KRONOS_DENIED (-2)    /* the plugin wasn't granted the capability */
#define KRONOS_NOT_FOUND (-3)
#define KRONOS_CONFLICT (-4)  /* name already registered at that version, or a channel with another layout */
#define KRONOS_INVALID (-5)

/* Capabilities a plugin asks for in KronosPluginInfo. Host calls outside
 * the granted set return KRONOS_DENIED. */
#define KRONOS_CAP_SCENE_READ (1u << 0)
#define KRONOS_CAP_SCENE_WRITE (1u << 1)
#define KRONOS_CAP_ASSET_IMPORTERS (1u << 2)
#define KRONOS_CAP_EDITOR_PANELS (1u << 3)
#define KRONOS_CAP_CHANNELS (1u << 4)

#define KRONOS_LOG_INFO 0
#define KRONOS_LOG_WARNING 1
#define KRONOS_LOG_ERROR 2

typedef struct KronosHost KronosHost;
typedef uint64_t KronosEntity; /* 0 means no entity */

typedef struct KronosPluginInfo {
    uint32_t struct_size;
    uint16_t api_major;
    uint16_t api_minor;
    const char* id;      /* unique and stable, e.g. "com.example.heightmaps" */
    const char* name;    /* shown in the editor */
    const char* version; /* e.g. "1.2.0" */
    const char* author;
    uint64_t capabilities;
} KronosPluginInfo;

/* Immediate-mode UI for editor panels. Widgets return nonzero when the user
 * changed them this frame. Labels identify widgets, so keep them unique
 * within a panel. */
typedef struct KronosUi {
    uint32_t struct_size;
    void* context;
    void (*text)(void* context, const char* text);
    int32_t (*button)(void* context, const char* label);
    int32_t (*checkbox)(void* context, const char* label, int32_t* value);
    int32_t (*slider_float)(void* context, const char* label, float* value, float min, float max);
    int32_t (*input_text)(void* context, const char* label, char* buffer, uint32_t capacity);
    void (*separator)(void* context);
    void (*same_line)(void* context);
} KronosUi;

typedef struct KronosAssetOutput {
    uint32_t struct_size;
    void* context;
    int32_t (*write)(void* context, const void* data, uint64_t size);
} KronosAssetOutput;

/* Converts files the engine can't read into ones it can. `type` names the
 * importer; when several plugins register the same type, the highest
 * version is used. */
typedef struct KronosAssetImporter {
    uint32_t struct_size;
    const char* type;
    uint32_t version;
    const char* extensions;       /* source extensions, ";"-separated: ".hgt;.ter" */
    const char* output_extension; /* a format the engine loads: ".png", ".obj", ".wav", ... */
    void* user;
    int32_t (*import)(void* user, const char* source_name, const uint8_t* data, uint64_t size,
                      const KronosAssetOutput* output);
} KronosAssetImporter;

typedef struct KronosPanel {
    uint32_t struct_size;
    const char* id;
    const char* title;
    uint32_t version;
    void* user;
    void (*draw)(void* user, const KronosUi* ui);
} KronosPanel;

/* A named block of memory shared between plugins and the engine. It
 * outlives the plugin that opened it, so it also keeps a plugin's state
 * across reloads. */
typedef struct KronosChannel {
    void* data;
    uint64_t size;
    uint32_t version;
} KronosChannel;

typedef struct KronosHostApi {
    uint32_t struct_size;
    uint16_t api_major;
    uint16_t api_minor;
    KronosHost* host;
    uint64_t granted_capabilities;
    int32_t sandboxed; /* nonzero when running in an isolated process */

    void (*log)(KronosHost* host, int32_t level, const char* message);

    /* Registrations copy the strings; function pointers must stay valid
     * until kronos_plugin_unload. Everything a plugin registered is removed
     * when it unloads. */
    int32_t (*register_asset_importer)(KronosHost* host, const KronosAssetImporter* importer);
    int32_t (*register_panel)(KronosHost* host, const KronosPanel* panel);

    /* Opens or creates a zero-filled channel. Opening an existing channel
     * with a different version or size returns KRONOS_CONFLICT. */
    int32_t (*open_channel)(KronosHost* host, const char* name, uint32_t version, uint64_t size,
                            KronosChannel* out);

    uint32_t (*entity_count)(KronosHost* host);
    KronosEntity (*entity_at)(KronosHost* host, uint32_t index);
    KronosEntity (*find_entity)(KronosHost* host, const char* name);
    /* Copies the name (truncated, always terminated) and returns its full length. */
    uint32_t (*entity_name)(KronosHost* host, KronosEntity entity, char* buffer, uint32_t capacity);
    int32_t (*get_position)(KronosHost* host, KronosEntity entity, float out[3]);
    int32_t (*set_position)(KronosHost* host, KronosEntity entity, const float position[3]);
} KronosHostApi;

/* Fill in `info` (struct_size is set by the host). */
typedef int32_t (*KronosPluginQueryFn)(KronosPluginInfo* info);
/* Register things and return an instance pointer passed to tick/unload.
 * `api` stays valid until unload. */
typedef int32_t (*KronosPluginLoadFn)(const KronosHostApi* api, void** instance);
typedef void (*KronosPluginTickFn)(void* instance, float dt);
typedef void (*KronosPluginUnloadFn)(void* instance);

#define KRONOS_PLUGIN_QUERY_SYMBOL "kronos_plugin_query"
#define KRONOS_PLUGIN_LOAD_SYMBOL "kronos_plugin_load"
#define KRONOS_PLUGIN_TICK_SYMBOL "kronos_plugin_tick"
#define KRONOS_PLUGIN_UNLOAD_SYMBOL "kronos_plugin_unload"

#endif
