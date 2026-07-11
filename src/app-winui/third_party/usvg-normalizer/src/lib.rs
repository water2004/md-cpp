use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr;
use std::sync::Arc;

use mermaid_rs_renderer::{RenderOptions, Theme, render_with_options};

pub struct Normalizer {
    fontdb: Option<Arc<usvg::fontdb::Database>>,
}

impl Normalizer {
    fn options(&mut self, source: &[u8], font_size: f32) -> usvg::Options<'static> {
        let has_text = source.windows(5).any(|part| part == b"<text")
            || source.windows(6).any(|part| part == b"<tspan");
        let mut options = usvg::Options::default();
        options.font_family = "Arial".to_string();
        if font_size.is_finite() && font_size > 0.0 {
            options.font_size = font_size;
        }
        options.languages = vec!["en".to_string(), "zh-CN".to_string()];
        if has_text {
            let fontdb = self.fontdb.get_or_insert_with(|| {
                let mut fontdb = usvg::fontdb::Database::new();
                fontdb.load_system_fonts();
                fontdb.set_serif_family("Times New Roman");
                fontdb.set_sans_serif_family("Arial");
                fontdb.set_cursive_family("Comic Sans MS");
                fontdb.set_fantasy_family("Impact");
                fontdb.set_monospace_family("Cascadia Code");
                Arc::new(fontdb)
            });
            options.fontdb = Arc::clone(fontdb);
        }
        options
    }
}

fn write_output(value: String, output: *mut *mut u8, output_len: *mut usize) {
    let data = value.into_bytes().into_boxed_slice();
    let len = data.len();
    let pointer = Box::into_raw(data) as *mut u8;
    unsafe {
        *output = pointer;
        *output_len = len;
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn elmd_svg_normalizer_create() -> *mut Normalizer {
    catch_unwind(AssertUnwindSafe(|| {
        Box::into_raw(Box::new(Normalizer { fontdb: None }))
    }))
    .unwrap_or(ptr::null_mut())
}

#[unsafe(no_mangle)]
pub extern "C" fn elmd_svg_normalizer_destroy(normalizer: *mut Normalizer) {
    if normalizer.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(normalizer));
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn elmd_svg_normalize(
    normalizer: *mut Normalizer,
    input: *const u8,
    input_len: usize,
    font_size: f32,
    output: *mut *mut u8,
    output_len: *mut usize,
) -> i32 {
    if normalizer.is_null() || input.is_null() || output.is_null() || output_len.is_null() {
        return -1;
    }
    unsafe {
        *output = ptr::null_mut();
        *output_len = 0;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let source = unsafe { std::slice::from_raw_parts(input, input_len) };
        let normalizer = unsafe { &mut *normalizer };
        let options = normalizer.options(source, font_size);
        match usvg::Tree::from_data(source, &options) {
            Ok(tree) => {
                write_output(
                    tree.to_string(&usvg::WriteOptions::default()),
                    output,
                    output_len,
                );
                0
            }
            Err(error) => {
                write_output(error.to_string(), output, output_len);
                1
            }
        }
    }))
    .unwrap_or(2)
}

#[unsafe(no_mangle)]
pub extern "C" fn elmd_mermaid_render(
    normalizer: *mut Normalizer,
    input: *const u8,
    input_len: usize,
    dark: bool,
    output: *mut *mut u8,
    output_len: *mut usize,
    width: *mut f32,
    height: *mut f32,
) -> i32 {
    if normalizer.is_null()
        || input.is_null()
        || output.is_null()
        || output_len.is_null()
        || width.is_null()
        || height.is_null()
    {
        return -1;
    }
    unsafe {
        *output = ptr::null_mut();
        *output_len = 0;
        *width = 0.0;
        *height = 0.0;
    }
    catch_unwind(AssertUnwindSafe(|| {
        let source = unsafe { std::slice::from_raw_parts(input, input_len) };
        let source = match std::str::from_utf8(source) {
            Ok(value) => value,
            Err(error) => {
                write_output(error.to_string(), output, output_len);
                return 1;
            }
        };
        let mut render_options = RenderOptions::default();
        if dark {
            render_options.theme = Theme::dark();
        }
        let rendered = match render_with_options(source, render_options) {
            Ok(value) => value,
            Err(error) => {
                write_output(error.to_string(), output, output_len);
                return 1;
            }
        };
        let normalizer = unsafe { &mut *normalizer };
        let options = normalizer.options(rendered.as_bytes(), 16.0);
        match usvg::Tree::from_data(rendered.as_bytes(), &options) {
            Ok(tree) => {
                let size = tree.size();
                unsafe {
                    *width = size.width();
                    *height = size.height();
                }
                write_output(
                    tree.to_string(&usvg::WriteOptions::default()),
                    output,
                    output_len,
                );
                0
            }
            Err(error) => {
                write_output(error.to_string(), output, output_len);
                1
            }
        }
    }))
    .unwrap_or(2)
}

#[unsafe(no_mangle)]
pub extern "C" fn elmd_svg_buffer_destroy(data: *mut u8, len: usize) {
    if data.is_null() {
        return;
    }
    unsafe {
        let slice = ptr::slice_from_raw_parts_mut(data, len);
        drop(Box::from_raw(slice));
    }
}
