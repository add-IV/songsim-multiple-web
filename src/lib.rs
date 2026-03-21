use std::collections::HashMap;
use std::ffi::CStr;
use std::io::Cursor;
use std::os::raw::c_char;

use image::{ImageBuffer, Rgba};

struct SongInfo {
    frequencies: HashMap<String, Vec<usize>>,
    word_count: usize,
}

fn calc_song_seq(song: &str) -> SongInfo {
    let mut frequencies: HashMap<String, Vec<usize>> = HashMap::new();
    let mut index = 0usize;
    for word in song.split_whitespace() {
        frequencies.entry(word.to_string()).or_default().push(index);
        index += 1;
    }
    SongInfo { frequencies, word_count: index }
}

fn make_png(song_info: &SongInfo) -> Option<Vec<u8>> {
    let n = song_info.word_count as u32;
    let mut img: ImageBuffer<Rgba<u8>, Vec<u8>> =
        ImageBuffer::from_fn(n, n, |_, _| Rgba([255, 255, 255, 255]));

    for locations in song_info.frequencies.values() {
        for (i, &occ1) in locations.iter().enumerate() {
            img.put_pixel(occ1 as u32, occ1 as u32, Rgba([222, 165, 132, 255]));
            for &occ2 in &locations[i + 1..] {
                img.put_pixel(occ1 as u32, occ2 as u32, Rgba([222, 165, 132, 255]));
                img.put_pixel(occ2 as u32, occ1 as u32, Rgba([222, 165, 132, 255]));
            }
        }
    }

    let mut buf = Vec::new();
    img.write_to(&mut Cursor::new(&mut buf), image::ImageFormat::Png).ok()?;
    Some(buf)
}

/// Returns a heap-allocated PNG byte buffer.
/// The caller must free it with `songsim_rust_free`.
/// Returns NULL on error or empty input.
#[unsafe(no_mangle)]
pub extern "C" fn songsim_rust(text: *const c_char, out_len: *mut usize) -> *mut u8 {
    let s = unsafe {
        if text.is_null() {
            return std::ptr::null_mut();
        }
        match CStr::from_ptr(text).to_str() {
            Ok(s) => s,
            Err(_) => return std::ptr::null_mut(),
        }
    };
    let song_info = calc_song_seq(s);
    if song_info.word_count == 0 {
        return std::ptr::null_mut();
    }
    let Some(png) = make_png(&song_info) else {
        return std::ptr::null_mut();
    };
    let len = png.len();
    unsafe { *out_len = len; }
    Box::into_raw(png.into_boxed_slice()) as *mut u8
}

#[unsafe(no_mangle)]
pub extern "C" fn songsim_rust_free(ptr: *mut u8, len: usize) {
    if !ptr.is_null() {
        unsafe {
            drop(Box::from_raw(std::slice::from_raw_parts_mut(ptr, len)));
        }
    }
}
