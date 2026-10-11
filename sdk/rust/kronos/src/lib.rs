// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Faris Nadim
//! Write Kronos Studio plugins in Rust.
//!
//! A safe layer over the C plugin API (`engine/src/plugin/kronos_plugin.h`): implement
//! [`Plugin`], register panels and importers through [`Host`], and export it with
//! [`export_plugin!`]. Build the crate as a `cdylib`. Works in-process and in the sandbox.
//! See `templates/rust-plugin` and `docs/PLUGIN_API.md`.

pub mod sys;

use core::ffi::{c_char, c_void, CStr};
use std::ffi::CString;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

/// Capabilities a plugin asks for in [`Info::capabilities`].
pub mod caps {
    pub const SCENE_READ: u64 = 1 << 0;
    pub const SCENE_WRITE: u64 = 1 << 1;
    pub const ASSET_IMPORTERS: u64 = 1 << 2;
    pub const EDITOR_PANELS: u64 = 1 << 3;
    pub const CHANNELS: u64 = 1 << 4;
}

/// What the host said no to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Failed,
    /// The plugin wasn't granted the capability.
    Denied,
    NotFound,
    /// Name already registered at that version, or a channel with another layout.
    Conflict,
    Invalid,
}

impl Error {
    pub fn code(self) -> i32 {
        match self {
            Error::Failed => sys::KRONOS_ERROR,
            Error::Denied => sys::KRONOS_DENIED,
            Error::NotFound => sys::KRONOS_NOT_FOUND,
            Error::Conflict => sys::KRONOS_CONFLICT,
            Error::Invalid => sys::KRONOS_INVALID,
        }
    }

    fn check(code: i32) -> Result<()> {
        match code {
            sys::KRONOS_OK => Ok(()),
            sys::KRONOS_DENIED => Err(Error::Denied),
            sys::KRONOS_NOT_FOUND => Err(Error::NotFound),
            sys::KRONOS_CONFLICT => Err(Error::Conflict),
            sys::KRONOS_INVALID => Err(Error::Invalid),
            _ => Err(Error::Failed),
        }
    }
}

pub type Result<T> = core::result::Result<T, Error>;

/// Shown in the editor's plugin list. Strings are C string literals: `c"My Plugin"`.
pub struct Info {
    /// Unique and stable, e.g. `c"com.example.heightmaps"`.
    pub id: &'static CStr,
    pub name: &'static CStr,
    pub version: &'static CStr,
    pub author: &'static CStr,
    pub capabilities: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogLevel {
    Info = 0,
    Warning = 1,
    Error = 2,
}

/// An object in the open scene.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Entity(pub u64);

/// Your plugin. Kronos creates it once in [`Plugin::load`] and drops it when the plugin unloads.
pub trait Plugin: Sized + 'static {
    const INFO: Info;
    fn load(host: Host) -> Result<Self>;
    fn tick(&mut self, _dt: f32) {}
}

/// The editor, as the plugin sees it. Cheap to copy; valid until the plugin unloads.
#[derive(Clone, Copy)]
pub struct Host {
    api: &'static sys::KronosHostApi,
}

fn c_text(text: &str) -> CString {
    let end = text.find('\0').unwrap_or(text.len());
    CString::new(&text[..end]).unwrap_or_default()
}

// Closures handed to the host; freed when the plugin unloads. The host only calls a plugin
// from one thread at a time, so they never run concurrently.
struct Registered(#[allow(dead_code)] Box<dyn std::any::Any>);
unsafe impl Send for Registered {}
static REGISTERED: Mutex<Vec<Registered>> = Mutex::new(Vec::new());

fn keep(value: Box<dyn std::any::Any>) {
    REGISTERED.lock().unwrap_or_else(|e| e.into_inner()).push(Registered(value));
}

impl Host {
    /// True when the plugin runs in its own locked-down process.
    pub fn sandboxed(&self) -> bool {
        self.api.sandboxed != 0
    }

    /// The capabilities the user granted (a subset of what [`Info`] asked for).
    pub fn granted(&self) -> u64 {
        self.api.granted_capabilities
    }

    pub fn log(&self, level: LogLevel, message: &str) {
        if let Some(log) = self.api.log {
            let message = c_text(message);
            unsafe { log(self.api.host, level as i32, message.as_ptr()) }
        }
    }

    pub fn entity_count(&self) -> u32 {
        self.api.entity_count.map_or(0, |f| unsafe { f(self.api.host) })
    }

    pub fn entity_at(&self, index: u32) -> Option<Entity> {
        let id = self.api.entity_at.map_or(0, |f| unsafe { f(self.api.host, index) });
        (id != 0).then_some(Entity(id))
    }

    pub fn entities(&self) -> impl Iterator<Item = Entity> + '_ {
        (0..self.entity_count()).filter_map(move |i| self.entity_at(i))
    }

    pub fn find_entity(&self, name: &str) -> Option<Entity> {
        let name = c_text(name);
        let id = self.api.find_entity.map_or(0, |f| unsafe { f(self.api.host, name.as_ptr()) });
        (id != 0).then_some(Entity(id))
    }

    pub fn entity_name(&self, entity: Entity) -> Option<String> {
        let f = self.api.entity_name?;
        let mut buffer = vec![0u8; 128];
        loop {
            let length = unsafe { f(self.api.host, entity.0, buffer.as_mut_ptr().cast(), buffer.len() as u32) } as usize;
            if length < buffer.len() {
                buffer.truncate(length);
                return String::from_utf8(buffer).ok();
            }
            buffer.resize(length + 1, 0);
        }
    }

    pub fn position(&self, entity: Entity) -> Result<[f32; 3]> {
        let f = self.api.get_position.ok_or(Error::Failed)?;
        let mut out = [0.0f32; 3];
        Error::check(unsafe { f(self.api.host, entity.0, out.as_mut_ptr()) })?;
        Ok(out)
    }

    pub fn set_position(&self, entity: Entity, position: [f32; 3]) -> Result<()> {
        let f = self.api.set_position.ok_or(Error::Failed)?;
        Error::check(unsafe { f(self.api.host, entity.0, position.as_ptr()) })
    }

    /// Adds an editor panel; `draw` runs every frame it's open.
    pub fn register_panel(&self, id: &str, title: &str, version: u32, draw: impl FnMut(&mut Ui) + 'static) -> Result<()> {
        unsafe extern "C" fn trampoline(user: *mut c_void, ui: *const sys::KronosUi) {
            if user.is_null() || ui.is_null() {
                return;
            }
            let draw = unsafe { &mut *(user as *mut Box<dyn FnMut(&mut Ui)>) };
            let mut ui = Ui { raw: unsafe { &*ui } };
            let _ = catch_unwind(AssertUnwindSafe(|| draw(&mut ui)));
        }
        let register = self.api.register_panel.ok_or(Error::Failed)?;
        let mut boxed: Box<Box<dyn FnMut(&mut Ui)>> = Box::new(Box::new(draw));
        let user = (&mut *boxed as *mut Box<dyn FnMut(&mut Ui)>).cast::<c_void>();
        let (id, title) = (c_text(id), c_text(title));
        let panel = sys::KronosPanel {
            struct_size: core::mem::size_of::<sys::KronosPanel>() as u32,
            id: id.as_ptr(),
            title: title.as_ptr(),
            version,
            user,
            draw: Some(trampoline),
        };
        Error::check(unsafe { register(self.api.host, &panel) })?;
        keep(boxed);
        Ok(())
    }

    /// Adds an asset importer for files ending in `extensions` (`".hgt;.ter"`), producing
    /// `output_extension` (a format the engine loads: `".png"`, `".obj"`, `".wav"`, ...).
    pub fn register_importer(
        &self,
        type_name: &str,
        version: u32,
        extensions: &str,
        output_extension: &str,
        import: impl FnMut(&str, &[u8], &mut Output) -> Result<()> + 'static,
    ) -> Result<()> {
        type ImportFn = Box<dyn FnMut(&str, &[u8], &mut Output) -> Result<()>>;
        unsafe extern "C" fn trampoline(
            user: *mut c_void,
            name: *const c_char,
            data: *const u8,
            size: u64,
            output: *const sys::KronosAssetOutput,
        ) -> i32 {
            if user.is_null() || output.is_null() || (data.is_null() && size != 0) {
                return sys::KRONOS_INVALID;
            }
            let import = unsafe { &mut *(user as *mut ImportFn) };
            let name = if name.is_null() { String::new() } else { unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned() };
            let bytes: &[u8] = if size == 0 { &[] } else { unsafe { core::slice::from_raw_parts(data, size as usize) } };
            let mut out = Output { raw: unsafe { &*output } };
            match catch_unwind(AssertUnwindSafe(|| import(&name, bytes, &mut out))) {
                Ok(Ok(())) => sys::KRONOS_OK,
                Ok(Err(error)) => error.code(),
                Err(_) => sys::KRONOS_ERROR,
            }
        }
        let register = self.api.register_asset_importer.ok_or(Error::Failed)?;
        let mut boxed: Box<ImportFn> = Box::new(Box::new(import));
        let user = (&mut *boxed as *mut ImportFn).cast::<c_void>();
        let (type_name, extensions, output_extension) = (c_text(type_name), c_text(extensions), c_text(output_extension));
        let importer = sys::KronosAssetImporter {
            struct_size: core::mem::size_of::<sys::KronosAssetImporter>() as u32,
            type_: type_name.as_ptr(),
            version,
            extensions: extensions.as_ptr(),
            output_extension: output_extension.as_ptr(),
            user,
            import: Some(trampoline),
        };
        Error::check(unsafe { register(self.api.host, &importer) })?;
        keep(boxed);
        Ok(())
    }

    /// Shared memory other plugins and tools can open by name.
    pub fn open_channel(&self, name: &str, version: u32, size: u64) -> Result<Channel> {
        let f = self.api.open_channel.ok_or(Error::Failed)?;
        let name = c_text(name);
        let mut raw = sys::KronosChannel { data: core::ptr::null_mut(), size: 0, version: 0 };
        Error::check(unsafe { f(self.api.host, name.as_ptr(), version, size, &mut raw) })?;
        Ok(Channel { raw })
    }
}

/// Shared memory from [`Host::open_channel`].
pub struct Channel {
    raw: sys::KronosChannel,
}

impl Channel {
    pub fn len(&self) -> usize {
        self.raw.size as usize
    }
    pub fn is_empty(&self) -> bool {
        self.raw.size == 0
    }
    pub fn version(&self) -> u32 {
        self.raw.version
    }
    pub fn as_ptr(&self) -> *mut u8 {
        self.raw.data.cast()
    }
    /// # Safety
    /// Other processes may write the same memory at any time; agree on a protocol with them.
    pub unsafe fn as_mut_slice(&mut self) -> &mut [u8] {
        unsafe { core::slice::from_raw_parts_mut(self.as_ptr(), self.len()) }
    }
}

/// Immediate-mode widgets for a panel's draw callback.
pub struct Ui<'a> {
    raw: &'a sys::KronosUi,
}

impl Ui<'_> {
    pub fn text(&mut self, text: &str) {
        if let Some(f) = self.raw.text {
            let text = c_text(text);
            unsafe { f(self.raw.context, text.as_ptr()) }
        }
    }

    /// True on the frame it's clicked.
    pub fn button(&mut self, label: &str) -> bool {
        let label = c_text(label);
        self.raw.button.is_some_and(|f| unsafe { f(self.raw.context, label.as_ptr()) } != 0)
    }

    /// True when the value changed.
    pub fn checkbox(&mut self, label: &str, value: &mut bool) -> bool {
        let Some(f) = self.raw.checkbox else { return false };
        let label = c_text(label);
        let mut flag = i32::from(*value);
        let changed = unsafe { f(self.raw.context, label.as_ptr(), &mut flag) } != 0;
        *value = flag != 0;
        changed
    }

    /// True when the value changed.
    pub fn slider(&mut self, label: &str, value: &mut f32, min: f32, max: f32) -> bool {
        let Some(f) = self.raw.slider_float else { return false };
        let label = c_text(label);
        unsafe { f(self.raw.context, label.as_ptr(), value, min, max) != 0 }
    }

    /// A text box holding at most `capacity - 1` bytes. True when the text changed.
    pub fn input_text(&mut self, label: &str, value: &mut String, capacity: usize) -> bool {
        let Some(f) = self.raw.input_text else { return false };
        let capacity = capacity.clamp(1, u32::MAX as usize);
        let label = c_text(label);
        let mut buffer = vec![0u8; capacity];
        let current = c_text(value);
        let bytes = current.as_bytes();
        let keep = bytes.len().min(capacity - 1);
        buffer[..keep].copy_from_slice(&bytes[..keep]);
        let changed = unsafe { f(self.raw.context, label.as_ptr(), buffer.as_mut_ptr().cast(), capacity as u32) } != 0;
        let end = buffer.iter().position(|&b| b == 0).unwrap_or(capacity - 1);
        *value = String::from_utf8_lossy(&buffer[..end]).into_owned();
        changed
    }

    pub fn separator(&mut self) {
        if let Some(f) = self.raw.separator {
            unsafe { f(self.raw.context) }
        }
    }

    pub fn same_line(&mut self) {
        if let Some(f) = self.raw.same_line {
            unsafe { f(self.raw.context) }
        }
    }
}

/// Where an importer writes the converted file.
pub struct Output<'a> {
    raw: &'a sys::KronosAssetOutput,
}

impl Output<'_> {
    pub fn write(&mut self, data: &[u8]) -> Result<()> {
        let f = self.raw.write.ok_or(Error::Failed)?;
        Error::check(unsafe { f(self.raw.context, data.as_ptr().cast(), data.len() as u64) })
    }
    pub fn write_str(&mut self, text: &str) -> Result<()> {
        self.write(text.as_bytes())
    }
}

#[doc(hidden)]
pub mod __export {
    use super::*;

    pub unsafe fn query<P: Plugin>(info: *mut sys::KronosPluginInfo) -> i32 {
        let Some(info) = (unsafe { info.as_mut() }) else { return sys::KRONOS_INVALID };
        info.api_major = sys::API_MAJOR;
        info.api_minor = sys::API_MINOR;
        info.id = P::INFO.id.as_ptr();
        info.name = P::INFO.name.as_ptr();
        info.version = P::INFO.version.as_ptr();
        info.author = P::INFO.author.as_ptr();
        info.capabilities = P::INFO.capabilities;
        sys::KRONOS_OK
    }

    pub unsafe fn load<P: Plugin>(api: *const sys::KronosHostApi, instance: *mut *mut c_void) -> i32 {
        if api.is_null() || instance.is_null() {
            return sys::KRONOS_INVALID;
        }
        let api: &'static sys::KronosHostApi = unsafe { &*api };
        if api.api_major != sys::API_MAJOR || (api.struct_size as usize) < core::mem::size_of::<sys::KronosHostApi>() {
            return sys::KRONOS_INVALID;
        }
        unsafe { *instance = core::ptr::null_mut() };
        match catch_unwind(|| P::load(Host { api })) {
            Ok(Ok(plugin)) => {
                unsafe { *instance = Box::into_raw(Box::new(plugin)).cast() };
                sys::KRONOS_OK
            }
            Ok(Err(error)) => error.code(),
            Err(_) => sys::KRONOS_ERROR,
        }
    }

    pub unsafe fn tick<P: Plugin>(instance: *mut c_void, dt: f32) {
        if let Some(plugin) = unsafe { instance.cast::<P>().as_mut() } {
            let _ = catch_unwind(AssertUnwindSafe(|| plugin.tick(dt)));
        }
    }

    pub unsafe fn unload<P: Plugin>(instance: *mut c_void) {
        let _ = catch_unwind(AssertUnwindSafe(|| {
            if !instance.is_null() {
                drop(unsafe { Box::from_raw(instance.cast::<P>()) });
            }
            REGISTERED.lock().unwrap_or_else(|e| e.into_inner()).clear();
        }));
    }
}

/// Exports the four entry points Kronos looks for. Use once, in the `cdylib` crate.
#[macro_export]
macro_rules! export_plugin {
    ($plugin:ty) => {
        #[no_mangle]
        pub unsafe extern "C" fn kronos_plugin_query(info: *mut $crate::sys::KronosPluginInfo) -> i32 {
            unsafe { $crate::__export::query::<$plugin>(info) }
        }
        #[no_mangle]
        pub unsafe extern "C" fn kronos_plugin_load(
            api: *const $crate::sys::KronosHostApi,
            instance: *mut *mut ::core::ffi::c_void,
        ) -> i32 {
            unsafe { $crate::__export::load::<$plugin>(api, instance) }
        }
        #[no_mangle]
        pub unsafe extern "C" fn kronos_plugin_tick(instance: *mut ::core::ffi::c_void, dt: f32) {
            unsafe { $crate::__export::tick::<$plugin>(instance, dt) }
        }
        #[no_mangle]
        pub unsafe extern "C" fn kronos_plugin_unload(instance: *mut ::core::ffi::c_void) {
            unsafe { $crate::__export::unload::<$plugin>(instance) }
        }
    };
}
