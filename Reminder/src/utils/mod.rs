// src/utils/mod.rs - Path and icon utilities for maximum portability

use std::path::{Path, PathBuf};
use std::env;
use image::{ImageBuffer, Rgba};

/// Gets the directory containing the currently running executable.
/// If it fails, falls back to the current working directory.
pub fn get_exe_dir() -> PathBuf {
    match env::current_exe() {
        Ok(path) => path
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| PathBuf::from(".")),
        Err(_) => PathBuf::from("."),
    }
}

/// Resolves a path relative to the executable's directory.
/// This guarantees absolute portability (e.g. running from a USB drive).
pub fn resolve_path<P: AsRef<Path>>(relative_path: P) -> PathBuf {
    get_exe_dir().join(relative_path)
}

/// Programmatically generates a sleek, high-quality 32x32 RGBA tray icon in-memory.
/// Renders a modern glowing electric-blue-to-purple gradient circle.
/// This prevents any crash or missing resource errors if assets are missing.
pub fn generate_fallback_icon_rgba() -> (Vec<u8>, u32, u32) {
    let width = 32;
    let height = 32;
    let mut img = ImageBuffer::new(width, height);

    for (x, y, pixel) in img.enumerate_pixels_mut() {
        // Center of the 32x32 grid is (15.5, 15.5)
        let dx = (x as f32) - 15.5;
        let dy = (y as f32) - 15.5;
        let dist = (dx * dx + dy * dy).sqrt();

        if dist <= 12.0 {
            // Gradient ratio from left to right (0.0 to 1.0)
            let ratio = (x as f32) / 32.0;
            
            // Interpolate between Electric Blue (#3b82f6) and Purple (#8b5cf6)
            let r = (59.0 * (1.0 - ratio) + 139.0 * ratio) as u8;
            let g = (130.0 * (1.0 - ratio) + 92.0 * ratio) as u8;
            let b = (246.0 * (1.0 - ratio) + 246.0 * ratio) as u8;
            
            // Smooth anti-aliased edge alpha blending
            let alpha = if dist > 11.0 {
                (255.0 * (12.0 - dist)) as u8
            } else {
                255
            };
            
            *pixel = Rgba([r, g, b, alpha]);
        } else {
            // Transparent background
            *pixel = Rgba([0, 0, 0, 0]);
        }
    }

    (img.into_raw(), width, height)
}
