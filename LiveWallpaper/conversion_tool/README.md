# ShaderToy GLSL to LiveWallpaper HLSL Conversion Tool
A lightweight, zero-dependency Python tool to convert standard GLSL ShaderToy shaders into DirectX 11 HLSL shaders compatible with the LiveWallpaper rendering engine.

## Features
- **Zero Dependencies**: Uses only the Python standard library. No `pip install` commands required!
- **Core GLSL mappings**: Automatically translates types (`vecN` to `floatN`, `matN` to `floatNxN`), and intrinsics (`fract` to `frac`, `mix` to `lerp`, `mod` to `fmod`).
- **Signature Translation**: Transforms `void mainImage(out vec4 fragColor, in vec2 fragCoord)` to standard DX11 `float4 main(VS_OUTPUT input) : SV_Target`.
- **Automatic Mouse Interaction (Parallax Sway)**: Automatically detects if the shader does not use mouse input. If it doesn't, it injects a universal mouse-controlled parallax sway so the wallpaper interacts with your desktop cursor!

## Usage
Simply run the script with the path to your ShaderToy `.glsl` file:

```bash
python convert.py path/to/shader.glsl
```

This will output `path/to/shader.hlsl` in the same directory.

### Specify custom output path:
```bash
python convert.py input_shader.glsl custom_output.hlsl
```
