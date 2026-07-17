use std::collections::BTreeMap;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr;
use std::sync::Arc;

use base64::Engine as _;
use mermaid_rs_renderer::{RenderOptions, Theme, render_with_options};

const MAX_EMBEDDED_SVG_BYTES: usize = 1024 * 1024;
const MAX_EMBEDDED_SVG_IMAGES: usize = 64;
const MAX_EMBEDDED_SVG_DEPTH: usize = 8;

fn decode_embedded_svg(href: &str) -> Option<Vec<u8>> {
    let (metadata, payload) = href.split_once(',')?;
    let metadata = metadata.to_ascii_lowercase();
    let mut parts = metadata.split(';');
    if parts.next()? != "data:image/svg+xml" || !parts.any(|part| part == "base64") {
        return None;
    }
    if payload.len() > MAX_EMBEDDED_SVG_BYTES * 2 {
        return None;
    }
    let compact: String = payload
        .chars()
        .filter(|ch| !ch.is_ascii_whitespace())
        .collect();
    let decoded = base64::engine::general_purpose::STANDARD
        .decode(compact)
        .ok()?;
    (decoded.len() <= MAX_EMBEDDED_SVG_BYTES).then_some(decoded)
}

fn escape_xml_attribute(value: &str) -> String {
    let mut escaped = String::with_capacity(value.len());
    for ch in value.chars() {
        match ch {
            '&' => escaped.push_str("&amp;"),
            '<' => escaped.push_str("&lt;"),
            '>' => escaped.push_str("&gt;"),
            '"' => escaped.push_str("&quot;"),
            '\'' => escaped.push_str("&apos;"),
            _ => escaped.push(ch),
        }
    }
    escaped
}

fn numeric_svg_length(value: &str) -> Option<&str> {
    let value = value.trim();
    let value = value.strip_suffix("px").unwrap_or(value).trim();
    let parsed = value.parse::<f32>().ok()?;
    (parsed.is_finite() && parsed > 0.0).then_some(value)
}

fn inline_svg_image(
    image: roxmltree::Node<'_, '_>,
    decoded: &[u8],
    depth: usize,
) -> Option<String> {
    let child_source = std::str::from_utf8(decoded).ok()?;
    let child_source = expand_embedded_svg_images_inner(child_source, depth);
    let child_document = roxmltree::Document::parse(&child_source).ok()?;
    let root = child_document.root_element();
    if root.tag_name().name() != "svg" {
        return None;
    }

    let mut attributes = BTreeMap::<String, String>::new();
    for attribute in root.attributes() {
        let name = attribute.name();
        if name != "xmlns" && name != "x" && name != "y" && name != "width" && name != "height" {
            attributes.insert(name.to_string(), attribute.value().to_string());
        }
    }
    if !attributes.contains_key("viewBox") {
        let width = root.attribute("width").and_then(numeric_svg_length);
        let height = root.attribute("height").and_then(numeric_svg_length);
        if let (Some(width), Some(height)) = (width, height) {
            attributes.insert("viewBox".to_string(), format!("0 0 {width} {height}"));
        }
    }
    for attribute in image.attributes() {
        if attribute.name() == "href" {
            continue;
        }
        attributes.insert(attribute.name().to_string(), attribute.value().to_string());
    }

    let mut replacement = String::from("<svg");
    for (name, value) in attributes {
        replacement.push(' ');
        replacement.push_str(&name);
        replacement.push_str("=\"");
        replacement.push_str(&escape_xml_attribute(&value));
        replacement.push('"');
    }
    replacement.push('>');
    let mut children = root.children();
    if let Some(first) = children.next() {
        let last = root.children().last().unwrap_or(first);
        replacement.push_str(&child_source[first.range().start..last.range().end]);
    }
    replacement.push_str("</svg>");
    Some(replacement)
}

fn expand_embedded_svg_images_inner(source: &str, depth: usize) -> String {
    if depth >= MAX_EMBEDDED_SVG_DEPTH {
        return source.to_string();
    }
    let Ok(document) = roxmltree::Document::parse(source) else {
        return source.to_string();
    };
    let mut replacements = Vec::new();
    for image in document
        .descendants()
        .filter(|node| node.is_element() && node.tag_name().name() == "image")
        .take(MAX_EMBEDDED_SVG_IMAGES)
    {
        let href = image
            .attributes()
            .find(|attribute| attribute.name() == "href")
            .map(|attribute| attribute.value());
        let Some(decoded) = href.and_then(decode_embedded_svg) else {
            continue;
        };
        if let Some(replacement) = inline_svg_image(image, &decoded, depth + 1) {
            replacements.push((image.range(), replacement));
        }
    }
    if replacements.is_empty() {
        return source.to_string();
    }
    let mut expanded = source.to_string();
    for (range, replacement) in replacements.into_iter().rev() {
        expanded.replace_range(range, &replacement);
    }
    expanded
}

fn expand_embedded_svg_images(source: &[u8]) -> Vec<u8> {
    let Ok(source) = std::str::from_utf8(source) else {
        return source.to_vec();
    };
    expand_embedded_svg_images_inner(source, 0).into_bytes()
}

pub struct Normalizer {
    fontdb: Option<Arc<usvg::fontdb::Database>>,
}

impl Normalizer {
    fn options(&mut self, source: &[u8], font_size: f32) -> usvg::Options<'static> {
        let has_text = source.windows(5).any(|part| part == b"<text")
            || source.windows(6).any(|part| part == b"<tspan");
        let mut options = usvg::Options::default();
        options.font_family = "Arial".to_string();
        options.image_href_resolver = usvg::ImageHrefResolver {
            resolve_data: usvg::ImageHrefResolver::default_data_resolver(),
            // SVG documents opened from Markdown are untrusted. Never let an
            // embedded image URI turn into an implicit filesystem read or a
            // second network fetch; the host resolves the top-level resource.
            resolve_string: Box::new(|_, _| None),
        };
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
        let source = expand_embedded_svg_images(source);
        let normalizer = unsafe { &mut *normalizer };
        let options = normalizer.options(&source, font_size);
        match usvg::Tree::from_data(&source, &options) {
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

#[cfg(test)]
mod tests {
    use super::*;

    fn normalize(source: &[u8]) -> (i32, String, f32, f32) {
        let normalizer = elmd_svg_normalizer_create();
        assert!(!normalizer.is_null());
        let mut output = ptr::null_mut();
        let mut output_len = 0;
        let mut width = 0.0;
        let mut height = 0.0;
        let status = elmd_svg_normalize(
            normalizer,
            source.as_ptr(),
            source.len(),
            16.0,
            &mut output,
            &mut output_len,
            &mut width,
            &mut height,
        );
        let value = if output.is_null() {
            String::new()
        } else {
            let bytes = unsafe { std::slice::from_raw_parts(output, output_len) };
            let value = String::from_utf8_lossy(bytes).into_owned();
            elmd_svg_buffer_destroy(output, output_len);
            value
        };
        elmd_svg_normalizer_destroy(normalizer);
        (status, value, width, height)
    }

    #[test]
    fn svg_normalization_reports_intrinsic_dimensions() {
        let source = br#"<svg xmlns="http://www.w3.org/2000/svg" width="120" height="80"><rect width="120" height="80"/></svg>"#;
        let (status, output, width, height) = normalize(source);
        assert_eq!(status, 0);
        assert!(!output.is_empty());
        assert_eq!(width, 120.0);
        assert_eq!(height, 80.0);
    }

    #[test]
    fn embedded_data_svg_logo_is_inlined_as_vector_content() {
        let logo = br#"<svg xmlns="http://www.w3.org/2000/svg" fill="white" viewBox="0 0 24 24"><title>Logo</title><path d="M0 0h24v24H0z"/></svg>"#;
        let encoded = base64::engine::general_purpose::STANDARD.encode(logo);
        let source = format!(
            r##"<svg xmlns="http://www.w3.org/2000/svg" width="65" height="20"><rect width="65" height="20" fill="#555"/><image x="5" y="3" width="14" height="14" href="data:image/svg+xml;base64,{encoded}"/><text x="25" y="15">badge</text></svg>"##
        );

        let expanded = expand_embedded_svg_images(source.as_bytes());
        let expanded = String::from_utf8(expanded).unwrap();
        assert!(!expanded.contains("<image"));
        assert!(expanded.contains("<path"));
        assert!(expanded.contains("viewBox=\"0 0 24 24\""));
        assert!(expanded.contains("x=\"5\""));
        assert!(expanded.contains("height=\"14\""));

        let (status, output, width, height) = normalize(source.as_bytes());
        assert_eq!(status, 0);
        assert!(!output.contains("<image"));
        assert!(output.contains("<path"));
        assert_eq!(width, 65.0);
        assert_eq!(height, 20.0);
    }

    #[test]
    fn external_and_malformed_image_references_are_not_expanded() {
        let external = r#"<svg xmlns="http://www.w3.org/2000/svg"><image href="https://example.com/logo.svg"/></svg>"#;
        assert_eq!(
            expand_embedded_svg_images(external.as_bytes()),
            external.as_bytes()
        );
        let (status, output, _, _) = normalize(external.as_bytes());
        assert_eq!(status, 0);
        assert!(!output.contains("<image"));
        let malformed = r#"<svg xmlns="http://www.w3.org/2000/svg"><image href="data:image/svg+xml;base64,not-base64!"/></svg>"#;
        assert_eq!(
            expand_embedded_svg_images(malformed.as_bytes()),
            malformed.as_bytes()
        );
    }
}
