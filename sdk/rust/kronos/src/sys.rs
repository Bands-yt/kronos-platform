// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Faris Nadim
//! Raw types from `engine/src/plugin/kronos_plugin.h`. Prefer the safe API in the crate root.
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

pub const API_MAJOR: u16 = 1;
pub const API_MINOR: u16 = 0;

pub const KRONOS_OK: i32 = 0;
pub const KRONOS_ERROR: i32 = -1;
pub const KRONOS_DENIED: i32 = -2;
pub const KRONOS_NOT_FOUND: i32 = -3;
pub const KRONOS_CONFLICT: i32 = -4;
pub const KRONOS_INVALID: i32 = -5;

pub type KronosEntity = u64;

#[repr(C)]
pub struct KronosHost {
    _private: [u8; 0],
}

#[repr(C)]
pub struct KronosPluginInfo {
    pub struct_size: u32,
    pub api_major: u16,
    pub api_minor: u16,
    pub id: *const c_char,
    pub name: *const c_char,
    pub version: *const c_char,
    pub author: *const c_char,
    pub capabilities: u64,
}

#[repr(C)]
pub struct KronosUi {
    pub struct_size: u32,
    pub context: *mut c_void,
    pub text: Option<unsafe extern "C" fn(*mut c_void, *const c_char)>,
    pub button: Option<unsafe extern "C" fn(*mut c_void, *const c_char) -> i32>,
    pub checkbox: Option<unsafe extern "C" fn(*mut c_void, *const c_char, *mut i32) -> i32>,
    pub slider_float: Option<unsafe extern "C" fn(*mut c_void, *const c_char, *mut f32, f32, f32) -> i32>,
    pub input_text: Option<unsafe extern "C" fn(*mut c_void, *const c_char, *mut c_char, u32) -> i32>,
    pub separator: Option<unsafe extern "C" fn(*mut c_void)>,
    pub same_line: Option<unsafe extern "C" fn(*mut c_void)>,
}

#[repr(C)]
pub struct KronosAssetOutput {
    pub struct_size: u32,
    pub context: *mut c_void,
    pub write: Option<unsafe extern "C" fn(*mut c_void, *const c_void, u64) -> i32>,
}

#[repr(C)]
pub struct KronosAssetImporter {
    pub struct_size: u32,
    pub type_: *const c_char,
    pub version: u32,
    pub extensions: *const c_char,
    pub output_extension: *const c_char,
    pub user: *mut c_void,
    pub import: Option<
        unsafe extern "C" fn(*mut c_void, *const c_char, *const u8, u64, *const KronosAssetOutput) -> i32,
    >,
}

#[repr(C)]
pub struct KronosPanel {
    pub struct_size: u32,
    pub id: *const c_char,
    pub title: *const c_char,
    pub version: u32,
    pub user: *mut c_void,
    pub draw: Option<unsafe extern "C" fn(*mut c_void, *const KronosUi)>,
}

#[repr(C)]
pub struct KronosChannel {
    pub data: *mut c_void,
    pub size: u64,
    pub version: u32,
}

#[repr(C)]
pub struct KronosHostApi {
    pub struct_size: u32,
    pub api_major: u16,
    pub api_minor: u16,
    pub host: *mut KronosHost,
    pub granted_capabilities: u64,
    pub sandboxed: i32,
    pub log: Option<unsafe extern "C" fn(*mut KronosHost, i32, *const c_char)>,
    pub register_asset_importer: Option<unsafe extern "C" fn(*mut KronosHost, *const KronosAssetImporter) -> i32>,
    pub register_panel: Option<unsafe extern "C" fn(*mut KronosHost, *const KronosPanel) -> i32>,
    pub open_channel:
        Option<unsafe extern "C" fn(*mut KronosHost, *const c_char, u32, u64, *mut KronosChannel) -> i32>,
    pub entity_count: Option<unsafe extern "C" fn(*mut KronosHost) -> u32>,
    pub entity_at: Option<unsafe extern "C" fn(*mut KronosHost, u32) -> KronosEntity>,
    pub find_entity: Option<unsafe extern "C" fn(*mut KronosHost, *const c_char) -> KronosEntity>,
    pub entity_name: Option<unsafe extern "C" fn(*mut KronosHost, KronosEntity, *mut c_char, u32) -> u32>,
    pub get_position: Option<unsafe extern "C" fn(*mut KronosHost, KronosEntity, *mut f32) -> i32>,
    pub set_position: Option<unsafe extern "C" fn(*mut KronosHost, KronosEntity, *const f32) -> i32>,
}
