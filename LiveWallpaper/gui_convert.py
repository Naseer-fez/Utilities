#!/usr/bin/env python3
"""
ShaderToy GLSL to LiveWallpaper HLSL - GUI Conversion Tool
==========================================================
A simple, zero-dependency GUI application to select and convert ShaderToy GLSL
shaders into DirectX 11 HLSL shaders compatible with the LiveWallpaper engine.

Saved in the workspace root directory for easy access.
"""

import os
import re
import tkinter as tk
from tkinter import filedialog, messagebox

HLSL_HEADER = """// =========================================================================
// Converted via LiveWallpaper Shader Conversion Tool (GUI)
// Compatible with DX11 HLSL (ps_4_0 / ps_5_0 profiles)
// =========================================================================

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
"""

def convert_glsl_to_hlsl(glsl_code):
    # Keep track of mouse usage
    has_mouse = "iMouse" in glsl_code
    
    # 1. Strip GLSL precision qualifiers
    code = re.sub(r'\b(lowp|mediump|highp)\b\s*', '', glsl_code)

    # 2. Convert standard GLSL types to HLSL
    code = re.sub(r'\bvec([234])\b', r'float\1', code)
    code = re.sub(r'\bbvec([234])\b', r'bool\1', code)
    code = re.sub(r'\bivec([234])\b', r'int\1', code)
    code = re.sub(r'\buvec([234])\b', r'uint\1', code)
    code = re.sub(r'\bmat([234])\b', r'float\1x\1', code)

    # 3. Convert standard GLSL intrinsic functions to HLSL
    code = re.sub(r'\bfract\b', 'frac', code)
    code = re.sub(r'\bmix\b', 'lerp', code)
    code = re.sub(r'\bmod\b', 'fmod', code)
    code = re.sub(r'\binversesqrt\b', 'rsqrt', code)
    code = re.sub(r'\bdFdx\b', 'ddx', code)
    code = re.sub(r'\bdFdy\b', 'ddy', code)

    # Convert 2-argument atan(y, x) to HLSL atan2(y, x)
    code = re.sub(r'\batan\s*\(\s*([^,]+)\s*,\s*([^)]+)\s*\)', r'atan2(\1, \2)', code)

    # 4. Handle ShaderToy mainImage entrypoint conversion
    main_pattern = r'void\s+mainImage\s*\(\s*out\s+(\w+)\s+(\w+)\s*,\s*in\s+(\w+)\s+(\w+)\s*\)'
    match = re.search(main_pattern, code)
    
    if match:
        color_type, color_name = match.group(1), match.group(2)
        coord_type, coord_name = match.group(3), match.group(4)
        
        hlsl_sig = "float4 main(VS_OUTPUT input) : SV_Target"
        code = re.sub(main_pattern, hlsl_sig, code)
        
        sig_index = code.find(hlsl_sig)
        if sig_index != -1:
            brace_index = code.find('{', sig_index)
            if brace_index != -1:
                injection = f"\n    float2 {coord_name} = input.TexCoord * iResolution.xy;"
                
                # If the shader does not already have mouse interaction, inject camera sway parallax
                if not has_mouse:
                    injection += f"\n    // Universal mouse interactive sway injected by Conversion Tool\n"
                    injection += f"    if (iMouse.x > 0.0f) {{\n"
                    injection += f"        {coord_name} += (iMouse.xy - iResolution.xy * 0.5f) * 0.15f;\n"
                    injection += f"    }}\n"
                
                injection += f"    float4 {color_name} = float4(0.0f, 0.0f, 0.0f, 1.0f);\n"
                
                code = code[:brace_index + 1] + injection + code[brace_index + 1:]
                
                last_brace = code.rfind('}')
                if last_brace != -1:
                    code = code[:last_brace] + f"\n    return {color_name};\n" + code[last_brace:]
    
    full_hlsl = HLSL_HEADER + "\n" + code
    return full_hlsl

class ShaderConverterGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("ShaderToy to HLSL Converter")
        self.root.geometry("520x220")
        self.root.resizable(False, False)
        
        # Configure grid expansion
        self.root.columnconfigure(0, weight=1)
        
        # Title Label
        title_label = tk.Label(
            root, 
            text="ShaderToy GLSL → DX11 HLSL", 
            font=("Arial", 14, "bold"),
            pady=15
        )
        title_label.grid(row=0, column=0, columnspan=3)

        # File Selection Row
        file_label = tk.Label(root, text="Select GLSL Shader:", font=("Arial", 10))
        file_label.grid(row=1, column=0, sticky="w", padx=20, pady=5)
        
        self.entry_path = tk.Entry(root, width=45, font=("Arial", 10))
        self.entry_path.grid(row=2, column=0, padx=(20, 5), pady=5, sticky="we")
        
        btn_browse = tk.Button(
            root, 
            text="Browse...", 
            command=self.browse_file,
            font=("Arial", 9)
        )
        btn_browse.grid(row=2, column=1, padx=(0, 20), pady=5)

        # Action Button Row
        self.btn_convert = tk.Button(
            root, 
            text="Convert Shader", 
            command=self.convert_file,
            font=("Arial", 11, "bold"),
            bg="#2a75d3",
            fg="white",
            padx=15,
            pady=5,
            state=tk.DISABLED
        )
        self.btn_convert.grid(row=3, column=0, columnspan=2, pady=20)
        
        # Track selected path
        self.selected_path = ""

    def browse_file(self):
        file_path = filedialog.askopenfilename(
            title="Select GLSL Shader file",
            filetypes=[
                ("GLSL Shaders (*.glsl)", "*.glsl"),
                ("Text Files (*.txt)", "*.txt"),
                ("All Files (*.*)", "*.*")
            ]
        )
        if file_path:
            self.selected_path = file_path
            self.entry_path.delete(0, tk.END)
            self.entry_path.insert(0, file_path)
            self.btn_convert.config(state=tk.NORMAL)

    def convert_file(self):
        path = self.entry_path.get().strip()
        if not path or not os.path.exists(path):
            messagebox.showerror("Error", "Please select a valid input shader file.")
            return
            
        try:
            with open(path, 'r', encoding='utf-8') as f:
                glsl_content = f.read()

            hlsl_content = convert_glsl_to_hlsl(glsl_content)
            
            # Determine output name in same folder
            base, _ = os.path.splitext(path)
            output_path = base + ".hlsl"
            
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(hlsl_content)
                
            messagebox.showinfo(
                "Success", 
                f"Shader successfully converted and saved as:\n{os.path.basename(output_path)}\n\nIn the same directory!"
            )
        except Exception as e:
            messagebox.showerror("Conversion Failed", f"An error occurred during conversion:\n{str(e)}")

def main():
    root = tk.Tk()
    app = ShaderConverterGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()
