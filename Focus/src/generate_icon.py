from PIL import Image

def generate_orange_icon():
    # Premium vibrant orange color: RGB (255, 102, 0)
    orange_color = (255, 102, 0, 255)
    
    # Create a 64x64 solid orange image
    img = Image.new('RGBA', (64, 64), color=orange_color)
    
    # Save it in src/logo.ico
    img.save('src/logo.ico', format='ICO', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)])
    
    # Save it in root directory as logo.ico
    img.save('logo.ico', format='ICO', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)])
    
    # Save it in root directory as logo.png
    img.save('logo.png', format='PNG')
    
    print("Successfully generated orange logo and icon files in src/ and root directory.")

if __name__ == '__main__':
    generate_orange_icon()

