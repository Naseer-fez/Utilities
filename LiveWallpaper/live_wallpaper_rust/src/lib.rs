use std::ffi::c_void;
use std::path::{Path, PathBuf};
use std::time::Instant;
use windows::core::*;
use windows::Win32::Foundation::*;
use windows::Win32::Graphics::Direct3D::*;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::Direct3D::Fxc::*;
use notify::{Watcher, RecommendedWatcher, Config, RecursiveMode};

// Uniform buffer layout matching HLSL cbuffer
#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct ShaderUniforms {
    i_time: f32,             // 4 bytes
    i_resolution: [f32; 3],  // 12 bytes
    i_mouse: [f32; 4],       // 16 bytes
    i_audio: [f32; 4],       // 16 bytes
    i_depth: f32,            // 4 bytes
    i_frame: i32,            // 4 bytes
    padding: [f32; 2],       // 8 bytes (align to 16-byte boundary, total 64 bytes)
}

struct ReloadState {
    shader_path: PathBuf,
    pending_shader_bytes: parking_lot::RwLock<Option<Vec<u8>>>,
    has_update: std::sync::atomic::AtomicBool,
}

struct WatcherHandle {
    _watcher: Option<RecommendedWatcher>,
    stop_signal: std::sync::Arc<std::sync::atomic::AtomicBool>,
    thread_handle: Option<std::thread::JoinHandle<()>>,
}

pub struct ShaderHost {
    device: ID3D11Device,
    context: ID3D11DeviceContext,
    
    // Shaders and constant buffers
    vertex_shader: ID3D11VertexShader,
    pixel_shader: Option<ID3D11PixelShader>,
    constant_buffer: ID3D11Buffer,
    
    // File tracking (async reload state)
    reload_state: std::sync::Arc<ReloadState>,
    watcher_handle: WatcherHandle,
    
    // Uniform tracking
    start_time: Instant,
    frame_count: i32,
}

// Global default vertex shader that generates a full-screen triangle using SV_VertexID
const DEFAULT_VS_CODE: &str = r#"
struct VS_OUTPUT {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT main(uint VertexID : SV_VertexID) {
    VS_OUTPUT Output;
    float2 baseUV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.TexCoord = baseUV;
    Output.Position = float4(baseUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}
"#;

// Fallback pixel shader in case loading/compiling user shader fails
const FALLBACK_PS_CODE: &str = r#"
cbuffer ShaderUniforms : register(b0) {
    float iTime;
    float3 iResolution;
    float4 iMouse;
    float4 iAudio;
    float iDepth;
    int iFrame;
};

struct VS_OUTPUT {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target {
    // Elegant fallback dark blue/indigo gradient with time-based breathing
    float2 uv = input.TexCoord;
    float pulse = sin(iTime * 0.5f) * 0.05f + 0.1f;
    float3 color = float3(0.02f, 0.04f, 0.12f) + float3(uv.x * 0.05f, uv.y * 0.08f, pulse);
    return float4(color, 1.0f);
}
"#;

impl ShaderHost {
    pub fn new(device: ID3D11Device, context: ID3D11DeviceContext, shader_path: &Path) -> Result<Self> {
        // Create Vertex Shader from DEFAULT_VS_CODE
        let vs_blob = compile_shader(DEFAULT_VS_CODE, "main", "vs_4_0")?;
        let mut vertex_shader = None;
        unsafe {
            let shader_data = std::slice::from_raw_parts(vs_blob.GetBufferPointer() as *const u8, vs_blob.GetBufferSize());
            device.CreateVertexShader(shader_data, None, Some(&mut vertex_shader))?;
        }
        let vertex_shader = vertex_shader.ok_or_else(|| Error::new(E_FAIL, "Failed to create Vertex Shader"))?;

        // Create constant buffer for uniforms
        let cb_desc = D3D11_BUFFER_DESC {
            ByteWidth: std::mem::size_of::<ShaderUniforms>() as u32,
            Usage: D3D11_USAGE_DYNAMIC,
            BindFlags: D3D11_BIND_CONSTANT_BUFFER.0 as u32,
            CPUAccessFlags: D3D11_CPU_ACCESS_WRITE.0 as u32,
            MiscFlags: 0,
            StructureByteStride: 0,
        };
        let mut constant_buffer = None;
        unsafe {
            device.CreateBuffer(&cb_desc, None, Some(&mut constant_buffer))?;
        }
        let constant_buffer = constant_buffer.ok_or_else(|| Error::new(E_FAIL, "Failed to create constant buffer"))?;

        // Compile the initial shader synchronously for immediate startup
        let user_code = std::fs::read_to_string(shader_path);
        let compile_res = match &user_code {
            Ok(code) => compile_shader(code, "main", "ps_4_0"),
            Err(_) => compile_shader(FALLBACK_PS_CODE, "main", "ps_4_0"),
        };
        let ps_blob = match compile_res {
            Ok(blob) => Some(blob),
            Err(e) => {
                eprintln!("[Rust Shader Host] Initial compilation error: {:?}", e);
                compile_shader(FALLBACK_PS_CODE, "main", "ps_4_0").ok()
            }
        };
        let mut pixel_shader = None;
        if let Some(blob) = ps_blob {
            unsafe {
                let shader_data = std::slice::from_raw_parts(blob.GetBufferPointer() as *const u8, blob.GetBufferSize());
                let _ = device.CreatePixelShader(shader_data, None, Some(&mut pixel_shader));
            }
        }

        // Initialize async file watcher state
        let stop_signal = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let reload_state = std::sync::Arc::new(ReloadState {
            shader_path: shader_path.to_path_buf(),
            pending_shader_bytes: parking_lot::RwLock::new(None),
            has_update: std::sync::atomic::AtomicBool::new(false),
        });

        // Setup notify watcher
        let (tx, rx) = std::sync::mpsc::channel();
        let mut watcher = RecommendedWatcher::new(tx, Config::default())
            .map_err(|e| Error::new(E_FAIL, format!("Failed to create watcher: {:?}", e)))?;

        // Watch parent directory
        let parent = shader_path.parent().unwrap_or_else(|| Path::new("."));
        let parent = if parent.as_os_str().is_empty() { Path::new(".") } else { parent };
        watcher.watch(parent, RecursiveMode::NonRecursive)
            .map_err(|e| Error::new(E_FAIL, format!("Failed to watch parent directory: {:?}", e)))?;

        let canonical_shader_path = std::fs::canonicalize(shader_path)
            .unwrap_or_else(|_| shader_path.to_path_buf());

        // Spawn background file checking and compilation thread
        let stop_signal_clone = stop_signal.clone();
        let reload_state_clone = reload_state.clone();
        
        let thread_handle = std::thread::spawn(move || {
            while !stop_signal_clone.load(std::sync::atomic::Ordering::Relaxed) {
                match rx.recv() {
                    Ok(Ok(event)) => {
                        let is_target = event.paths.iter().any(|p| {
                            std::fs::canonicalize(p)
                                .map(|cp| cp == canonical_shader_path)
                                .unwrap_or_else(|_| p == &canonical_shader_path)
                        });

                        if is_target {
                            // Debounce: sleep 100ms
                            std::thread::sleep(std::time::Duration::from_millis(100));

                            // Drain any subsequent rapid events
                            while rx.try_recv().is_ok() {}

                            if stop_signal_clone.load(std::sync::atomic::Ordering::Relaxed) {
                                break;
                            }

                            let user_code = std::fs::read_to_string(&reload_state_clone.shader_path);
                            let compile_res = match &user_code {
                                Ok(code) => compile_shader(code, "main", "ps_4_0"),
                                Err(_) => compile_shader(FALLBACK_PS_CODE, "main", "ps_4_0"),
                            };

                            let ps_blob = match compile_res {
                                Ok(blob) => Some(blob),
                                Err(e) => {
                                    eprintln!("[Rust Shader Host] Background compilation error: {:?}", e);
                                    compile_shader(FALLBACK_PS_CODE, "main", "ps_4_0").ok()
                                }
                            };

                            if let Some(blob) = ps_blob {
                                unsafe {
                                    let bytes = std::slice::from_raw_parts(
                                        blob.GetBufferPointer() as *const u8,
                                        blob.GetBufferSize()
                                    ).to_vec();

                                    let mut pending = reload_state_clone.pending_shader_bytes.write();
                                    *pending = Some(bytes);
                                    reload_state_clone.has_update.store(true, std::sync::atomic::Ordering::Release);
                                }
                            }
                        }
                    }
                    Ok(Err(e)) => {
                        eprintln!("[Rust Shader Host] Watcher event error: {:?}", e);
                    }
                    Err(_) => {
                        // Channel closed (watcher dropped)
                        break;
                    }
                }
            }
        });

        let watcher_handle = WatcherHandle {
            _watcher: Some(watcher),
            stop_signal,
            thread_handle: Some(thread_handle),
        };

        Ok(Self {
            device,
            context,
            vertex_shader,
            pixel_shader,
            constant_buffer,
            reload_state,
            watcher_handle,
            start_time: Instant::now(),
            frame_count: 0,
        })
    }

    pub fn reload_shader(&mut self) {
        // Non-blocking query of the atomic reload flag
        if !self.reload_state.has_update.load(std::sync::atomic::Ordering::Acquire) {
            return;
        }
        if let Some(mut pending) = self.reload_state.pending_shader_bytes.try_write() {
            if let Some(bytes) = pending.take() {
                let mut pixel_shader = None;
                unsafe {
                    let hr = self.device.CreatePixelShader(&bytes, None, Some(&mut pixel_shader));
                    if hr.is_ok() && pixel_shader.is_some() {
                        self.pixel_shader = pixel_shader;
                    } else {
                        eprintln!("[Rust Shader Host] Failed to create Pixel Shader from background compiled bytes. HR: {:?}", hr);
                    }
                }
            }
            self.reload_state.has_update.store(false, std::sync::atomic::Ordering::Release);
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render(
        &mut self, 
        rtv: &ID3D11RenderTargetView, 
        width: u32, 
        height: u32, 
        mouse_x: f32, 
        mouse_y: f32, 
        is_mouse_down: bool, 
        audio_data: &[f32; 4]
    ) -> Result<()> {
        // Auto hot reload check (debounced safely inside)
        self.reload_shader();

        // Update uniforms
        let i_time = self.start_time.elapsed().as_secs_f32();
        
        let click_val = if is_mouse_down { 1.0 } else { 0.0 };
        
        let uniforms = ShaderUniforms {
            i_time,
            i_resolution: [width as f32, height as f32, (width as f32) / (height as f32).max(1.0)],
            i_mouse: [mouse_x, mouse_y, click_val, 0.0],
            i_audio: *audio_data,
            i_depth: 0.0,
            i_frame: self.frame_count,
            padding: [0.0; 2],
        };

        self.frame_count += 1;

        unsafe {
            // Map uniform buffer
            let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
            self.context.Map(&self.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut mapped as *mut D3D11_MAPPED_SUBRESOURCE))?;
            std::ptr::copy_nonoverlapping(&uniforms, mapped.pData as *mut ShaderUniforms, 1);
            self.context.Unmap(&self.constant_buffer, 0);

            // Bind resources
            let viewport = D3D11_VIEWPORT {
                TopLeftX: 0.0,
                TopLeftY: 0.0,
                Width: width as f32,
                Height: height as f32,
                MinDepth: 0.0,
                MaxDepth: 1.0,
            };
            self.context.RSSetViewports(Some(&[viewport]));
            self.context.OMSetRenderTargets(Some(&[Some(rtv.clone())]), None);

            // Set shaders and buffers
            self.context.VSSetShader(&self.vertex_shader, None);
            if let Some(ps) = &self.pixel_shader {
                self.context.PSSetShader(ps, None);
            }
            self.context.VSSetConstantBuffers(0, Some(&[Some(self.constant_buffer.clone())]));
            self.context.PSSetConstantBuffers(0, Some(&[Some(self.constant_buffer.clone())]));

            self.context.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            self.context.IASetInputLayout(None);

            // Draw fullscreen quad (1 triangle SV_VertexID layout)
            self.context.Draw(3, 0);
        }

        Ok(())
    }
}

// Clean up background thread deterministically when ShaderHost is dropped
impl Drop for ShaderHost {
    fn drop(&mut self) {
        self.watcher_handle.stop_signal.store(true, std::sync::atomic::Ordering::Relaxed);
        self.watcher_handle._watcher = None; // Drop watcher first to close channel and wake up rx.recv()
        if let Some(handle) = self.watcher_handle.thread_handle.take() {
            let _ = handle.join();
        }
    }
}

// Compile HLSL source using direct D3DCompile API
fn compile_shader(source: &str, entry: &str, target: &str) -> Result<ID3DBlob> {
    let mut shader_blob = None;
    let mut error_blob = None;

    let entry_c = std::ffi::CString::new(entry).map_err(|_| Error::new(E_FAIL, "Invalid entry string"))?;
    let target_c = std::ffi::CString::new(target).map_err(|_| Error::new(E_FAIL, "Invalid target string"))?;

    unsafe {
        let hr = D3DCompile(
            source.as_ptr() as *const c_void,
            source.len(),
            None,
            None,
            None,
            PCSTR::from_raw(entry_c.as_ptr() as *const u8),
            PCSTR::from_raw(target_c.as_ptr() as *const u8),
            0,
            0,
            &mut shader_blob,
            Some(&mut error_blob),
        );

        if let Err(e) = hr {
            if let Some(errors) = error_blob {
                let err_msg = std::slice::from_raw_parts(
                    errors.GetBufferPointer() as *const u8,
                    errors.GetBufferSize(),
                );
                let err_str = String::from_utf8_lossy(err_msg).to_string();
                return Err(Error::new(e.code(), err_str));
            }
            return Err(e);
        }

        shader_blob.ok_or_else(|| Error::new(E_FAIL, "Shader blob was null despite success HRESULT"))
    }
}

// Helper to safely extract COM pointers from C++ raw pointers with AddRef
unsafe fn clone_com_from_raw<T: Interface>(raw: *mut c_void) -> Option<T> {
    if raw.is_null() {
        return None;
    }
    // Wrap the raw pointer in windows-rs IUnknown without taking ownership (via ManuallyDrop)
    let unk = std::mem::ManuallyDrop::new(unsafe { IUnknown::from_raw(raw) });
    // clone() calls AddRef, ensuring we own a new reference count
    let cloned_unk = (*unk).clone();
    // Cast to the target interface T
    cloned_unk.cast::<T>().ok()
}

// FFI Boundaries with panic protection
/// # Safety
///
/// This function is unsafe because it dereferences raw pointers passed from C++.
/// The caller must ensure that `device_raw`, `context_raw`, `shader_path_utf16`,
/// and `out_host` are valid, properly aligned, and live pointers.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn init_shader_host(
    device_raw: *mut c_void,
    context_raw: *mut c_void,
    shader_path_utf16: *const u16,
    out_host: *mut *mut ShaderHost
) -> HRESULT {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if device_raw.is_null() || context_raw.is_null() || shader_path_utf16.is_null() || out_host.is_null() {
            return E_POINTER;
        }

        let device = match unsafe { clone_com_from_raw::<ID3D11Device>(device_raw) } {
            Some(d) => d,
            None => return E_NOINTERFACE,
        };
        
        let context = match unsafe { clone_com_from_raw::<ID3D11DeviceContext>(context_raw) } {
            Some(c) => c,
            None => return E_NOINTERFACE,
        };

        // Parse wide UTF-16 string path with safety bounds limit
        let mut len = 0;
        loop {
            if len >= 32768 {
                return E_INVALIDARG;
            }
            if unsafe { *shader_path_utf16.add(len) } == 0 {
                break;
            }
            len += 1;
        }
        let slice = unsafe { std::slice::from_raw_parts(shader_path_utf16, len) };
        let path_str = String::from_utf16_lossy(slice);
        let path = Path::new(&path_str);

        match ShaderHost::new(device, context, path) {
            Ok(host) => {
                unsafe { *out_host = Box::into_raw(Box::new(host)); }
                S_OK
            }
            Err(e) => e.code(),
        }
    }));

    match result {
        Ok(hr) => hr,
        Err(_) => {
            eprintln!("[Rust FFI] Panic caught in init_shader_host!");
            E_FAIL
        }
    }
}

/// # Safety
///
/// This function is unsafe because it dereferences raw pointers passed from C++.
/// The caller must ensure that `host_ptr` points to a valid `ShaderHost` instance
/// and `rtv_raw` points to a valid ID3D11RenderTargetView. If `audio_len` is greater
/// than 0, `audio_data_ptr` must point to a valid array of floats of at least that size.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn render_shader_frame(
    host_ptr: *mut ShaderHost,
    rtv_raw: *mut c_void,
    width: u32,
    height: u32,
    mouse_x: f32,
    mouse_y: f32,
    is_mouse_down: bool,
    audio_data_ptr: *const f32,
    audio_len: u32
) -> HRESULT {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if host_ptr.is_null() || rtv_raw.is_null() {
            return E_POINTER;
        }

        if audio_len > 0 && audio_data_ptr.is_null() {
            return E_POINTER;
        }
        
        let rtv = match unsafe { clone_com_from_raw::<ID3D11RenderTargetView>(rtv_raw) } {
            Some(r) => r,
            None => return E_NOINTERFACE,
        };

        let host = unsafe { &mut *host_ptr };

        let mut audio_data = [0.0f32; 4];
        if audio_len > 0 {
            let read_len = std::cmp::min(audio_len as usize, 4);
            let slice = unsafe { std::slice::from_raw_parts(audio_data_ptr, read_len) };
            audio_data[..read_len].copy_from_slice(slice);
        }

        match host.render(&rtv, width, height, mouse_x, mouse_y, is_mouse_down, &audio_data) {
            Ok(_) => S_OK,
            Err(e) => e.code(),
        }
    }));

    match result {
        Ok(hr) => hr,
        Err(_) => {
            eprintln!("[Rust FFI] Panic caught in render_shader_frame!");
            E_FAIL
        }
    }
}

/// # Safety
///
/// This function is unsafe because it takes ownership of and deallocates the
/// raw `ShaderHost` pointer. The caller must ensure `host_ptr` is a valid pointer
/// previously returned by `init_shader_host`, and must not use `host_ptr` after
/// calling this function.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn shutdown_shader_host(host_ptr: *mut ShaderHost) -> HRESULT {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if host_ptr.is_null() {
            return E_POINTER;
        }
        // Reclaim memory and let Rust's RAII drop it deterministically
        let _ = unsafe { Box::from_raw(host_ptr) };
        S_OK
    }));

    match result {
        Ok(hr) => hr,
        Err(_) => {
            eprintln!("[Rust FFI] Panic caught in shutdown_shader_host!");
            E_FAIL
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_uniform_size() {
        assert_eq!(
            std::mem::size_of::<ShaderUniforms>(),
            64,
            "ShaderUniforms must be exactly 64 bytes (16-byte aligned) for HLSL constant buffers"
        );
    }

    #[test]
    fn test_init_shader_host_null_ptrs() {
        unsafe {
            // All null
            let hr = init_shader_host(
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null(),
                std::ptr::null_mut()
            );
            assert_eq!(hr, E_POINTER);

            // Some null
            let dummy_device = std::ptr::dangling_mut::<c_void>();
            let hr = init_shader_host(
                dummy_device,
                std::ptr::null_mut(),
                std::ptr::null(),
                std::ptr::null_mut()
            );
            assert_eq!(hr, E_POINTER);
        }
    }

    #[test]
    fn test_render_shader_frame_null_ptrs() {
        unsafe {
            let hr = render_shader_frame(
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                800,
                600,
                0.0,
                0.0,
                false,
                std::ptr::null(),
                0
            );
            assert_eq!(hr, E_POINTER);
        }
    }

    #[test]
    fn test_shutdown_shader_host_null_ptr() {
        unsafe {
            let hr = shutdown_shader_host(std::ptr::null_mut());
            assert_eq!(hr, E_POINTER);
        }
    }

    #[test]
    fn test_compile_shader_invalid_entry() {
        let res = compile_shader("void main() {}", "main\0", "vs_4_0");
        assert!(res.is_err());
    }
}
