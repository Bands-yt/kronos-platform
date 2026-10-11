// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Faris Nadim
//! Template Kronos plugin in Rust: a "Grid Snap" panel that snaps every object to a grid,
//! and an importer that turns PGM heightmaps into OBJ terrain. Same as
//! native_plugins/SampleCPlugin.c. Build with `cargo build --release` and load the library
//! from Studio's Plugins window. See docs/PLUGIN_API.md "Rust".

use std::cell::RefCell;
use std::rc::Rc;

use kronos::{caps, Host, Info, Output, Plugin, Ui};

struct GridSnap {
    grid: f32,
    snap_height: bool,
    status: String,
}

fn snap(value: f32, grid: f32) -> f32 {
    (value / grid).round() * grid
}

impl GridSnap {
    fn snap_all(&mut self, host: &Host) {
        let mut moved = 0;
        let count = host.entity_count();
        for entity in host.entities() {
            let Ok(p) = host.position(entity) else { continue };
            let height = if self.snap_height { snap(p[1], self.grid) } else { p[1] };
            let s = [snap(p[0], self.grid), height, snap(p[2], self.grid)];
            if s != p && host.set_position(entity, s).is_ok() {
                moved += 1;
            }
        }
        self.status = format!("Snapped {moved} of {count} objects.");
    }

    fn draw(&mut self, host: &Host, ui: &mut Ui) {
        ui.text(&format!("{} objects in the scene", host.entity_count()));
        ui.slider("Grid size", &mut self.grid, 0.25, 8.0);
        self.grid = self.grid.max(0.01);
        ui.checkbox("Snap height too", &mut self.snap_height);
        if ui.button("Snap everything") {
            self.snap_all(host);
        }
        if !self.status.is_empty() {
            ui.separator();
            ui.text(&self.status);
        }
    }
}

/// Reads whitespace-separated numbers from a PGM header or ASCII body, skipping `#` comments.
struct Cursor<'a> {
    data: &'a [u8],
    at: usize,
}

impl Cursor<'_> {
    fn number(&mut self) -> Option<u32> {
        while let Some(&c) = self.data.get(self.at) {
            if c == b'#' {
                while self.data.get(self.at).is_some_and(|&c| c != b'\n') {
                    self.at += 1;
                }
            } else if c.is_ascii_whitespace() {
                self.at += 1;
            } else {
                break;
            }
        }
        let start = self.at;
        while self.data.get(self.at).is_some_and(|c| c.is_ascii_digit()) && self.at - start < 9 {
            self.at += 1;
        }
        std::str::from_utf8(&self.data[start..self.at]).ok()?.parse().ok()
    }
}

fn import_heightmap(data: &[u8], out: &mut Output) -> kronos::Result<()> {
    let binary = match data {
        [b'P', b'2', ..] => false,
        [b'P', b'5', ..] => true,
        _ => return Err(kronos::Error::Invalid),
    };
    let mut c = Cursor { data, at: 2 };
    let (Some(width), Some(height), Some(max)) = (c.number(), c.number(), c.number()) else {
        return Err(kronos::Error::Invalid);
    };
    if !(2..=2048).contains(&width) || !(2..=2048).contains(&height) || !(1..=65535).contains(&max) {
        return Err(kronos::Error::Invalid);
    }
    let (width, height) = (width as usize, height as usize);
    let wide = max > 255;
    if binary {
        c.at += 1;
        if data.len().saturating_sub(c.at) < width * height * if wide { 2 } else { 1 } {
            return Err(kronos::Error::Invalid);
        }
    }

    let (spacing, tallest) = (1.0f32, 10.0f32);
    let mut text = String::from("# Terrain made from a heightmap by the Kronos sample plugin\no Terrain\n");
    for z in 0..height {
        for x in 0..width {
            let value = if binary {
                let v = if wide { u32::from(data[c.at]) << 8 | u32::from(data[c.at + 1]) } else { u32::from(data[c.at]) };
                c.at += if wide { 2 } else { 1 };
                v
            } else {
                c.number().ok_or(kronos::Error::Invalid)?
            };
            let y = value as f32 / max as f32 * tallest;
            let px = (x as f32 - (width - 1) as f32 * 0.5) * spacing;
            let pz = (z as f32 - (height - 1) as f32 * 0.5) * spacing;
            text += &format!("v {px:.3} {y:.3} {pz:.3}\n");
        }
    }
    for z in 0..height - 1 {
        for x in 0..width - 1 {
            let a = z * width + x + 1;
            let (b, d) = (a + 1, a + width);
            let e = d + 1;
            text += &format!("f {a} {d} {b}\nf {b} {d} {e}\n");
        }
    }
    out.write_str(&text)
}

struct SamplePlugin {
    _grid: Rc<RefCell<GridSnap>>,
}

impl Plugin for SamplePlugin {
    const INFO: Info = Info {
        id: c"kronos.sample.rust",
        name: c"Grid Snap and Heightmaps (Rust)",
        version: c"1.0.0",
        author: c"Kronos",
        capabilities: caps::SCENE_READ | caps::SCENE_WRITE | caps::EDITOR_PANELS | caps::ASSET_IMPORTERS,
    };

    fn load(host: Host) -> kronos::Result<Self> {
        let grid = Rc::new(RefCell::new(GridSnap { grid: 1.0, snap_height: false, status: String::new() }));
        let panel = grid.clone();
        host.register_panel("kronos.sample.rust.grid-snap", "Grid Snap (Rust)", 1, move |ui| {
            panel.borrow_mut().draw(&host, ui)
        })?;
        host.register_importer("kronos.sample.rust.heightmap", 1, ".pgm", ".obj", |_name, data, out| {
            import_heightmap(data, out)
        })?;
        Ok(SamplePlugin { _grid: grid })
    }
}

kronos::export_plugin!(SamplePlugin);
