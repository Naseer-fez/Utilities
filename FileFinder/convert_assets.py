#!/usr/bin/env python3
"""
FileFinder - Asset Conversion Helper Script
Converts source images (PNG, JPG, WebP, etc.) in the assets directory
into native Windows resource formats (ICO for icon, BMP for logo bitmap).
"""

import os
import sys

def check_pillow():
    try:
        from PIL import Image
        return True
    except ImportError:
        print("=" * 60)
        print("[ERROR] 'Pillow' library is not installed!")
        print("To install it, run:")
        print("    pip install Pillow")
        print("=" * 60)
        return False

def convert_image(source_path, dest_path, fmt):
    from PIL import Image
    try:
        print(f"[CONVERTING] '{source_path}' -> '{dest_path}'...")
        with Image.open(source_path) as img:
            # For icon, ensure we have standard icon sizes if converting to ICO
            if fmt.upper() == 'ICO':
                # Save as ICO with standard sizes
                img.save(dest_path, format='ICO', sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
            else:
                img.save(dest_path, format=fmt)
        print(f"[SUCCESS] Generated '{dest_path}' ({os.path.getsize(dest_path)} bytes)")
        return True
    except Exception as e:
        print(f"[ERROR] Failed to convert '{source_path}': {e}")
        return False

def main():
    print("============================================")
    print("  FileFinder - Windows Asset Converter")
    print("============================================")

    if not check_pillow():
        sys.exit(1)

    assets_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
    if not os.path.isdir(assets_dir):
        print(f"[ERROR] Assets directory not found at '{assets_dir}'")
        sys.exit(1)

    # Valid image extensions to search for
    extensions = [".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif"]
    
    # 1. Process Icon
    icon_source = None
    for ext in extensions:
        # Search for files like icon.png, icon.jpg, etc.
        test_path = os.path.join(assets_dir, f"icon{ext}")
        if os.path.isfile(test_path):
            icon_source = test_path
            break
            
    if icon_source:
        dest_icon = os.path.join(assets_dir, "icon.ico")
        convert_image(icon_source, dest_icon, "ICO")
    else:
        print("[SKIP] No source icon file found (e.g., 'assets/icon.png', 'assets/icon.jpg').")

    # 2. Process Logo
    logo_source = None
    for ext in extensions:
        # Search for files like logo.png, logo.jpg, etc.
        test_path = os.path.join(assets_dir, f"logo{ext}")
        # Make sure we don't treat the target logo.bmp as the source if we have a png/jpg source
        if ext == ".bmp":
            # Only use logo.bmp as source if there is no other logo source
            if os.path.isfile(test_path) and not logo_source:
                logo_source = test_path
        else:
            if os.path.isfile(test_path):
                logo_source = test_path
                break

    if logo_source:
        dest_logo = os.path.join(assets_dir, "logo.bmp")
        convert_image(logo_source, dest_logo, "BMP")
    else:
        print("[SKIP] No source logo file found (e.g., 'assets/logo.png', 'assets/logo.jpg').")

    print("\n[DONE] Asset conversion process completed.")

if __name__ == "__main__":
    main()
