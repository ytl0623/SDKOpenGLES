# import numpy as np
# from PIL import Image, ImageDraw, ImageFont

# # 定義灰階值
# gray_values = [90, 150, 180, 200, 250]

# # 圖片尺寸
# width, height = 512, 512

# for gray_value in gray_values:
#     # 創建灰階圖片（背景）
#     img = Image.new('RGB', (width, height), color=(gray_value, gray_value, gray_value))
    
#     # 創建繪圖物件
#     draw = ImageDraw.Draw(img)
    
#     # 設定字體大小（使用預設字體）
#     font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 500)
        
    
#     # 計算文字位置（置中）
#     # 使用 textbbox 來獲取文字邊界框
#     bbox = draw.textbbox((0, 0), "F", font=font)
#     text_width = bbox[2] - bbox[0]
#     text_height = bbox[3] - bbox[1]
    
#     position = ((width - text_width) // 2 - bbox[0], 
#                 (height - text_height) // 2 - bbox[1])
    
#     # 繪製 F 字母（使用對比色）
#     # 如果背景較暗，用白色；如果背景較亮，用黑色
#     text_color = (0, 0, 0)
#     draw.text(position, "F", fill=text_color, font=font)
    
#     # 儲存為 24-bit BMP
#     filename = f'gray_{gray_value}.bmp'
#     img.save(filename, 'BMP')
#     print(f'已產生: {filename}')

# print('\n所有圖片已產生完成！')

# from PIL import Image

# # 定義藍色亮度值 (對應 B 通道)
# blue_values = [32, 64, 128, 192, 255]

# # 圖片尺寸
# width, height = 1920, 1080

# print(f"開始產生 {width}x{height} 的藍色階調圖片...\n")

# for val in blue_values:
#     # 設定顏色: Red=0, Green=0, Blue=val
#     # val 越小越暗(深藍)，255 為最亮(純藍)
#     color = (val, val, val)
    
#     # 創建圖片 (RGB 模式自動存為 24-bit)
#     img = Image.new('RGB', (width, height), color=color)
    
#     # 儲存為 BMP
#     filename = f'white_{val}.bmp'
#     img.save(filename, 'BMP')
    
#     print(f'已產生: {filename} (RGB數值: {color})')

# print('\n所有圖片已產生完成！')

# 產生灰階圖
import numpy as np
from PIL import Image

def create_grayscale_gradient_bmp():
    # 1. 設定圖片尺寸
    width, height = 1920, 1080
    output_filename = 'gradient.bmp'

    print(f"正在生成 {width}x{height} 的灰階漸層圖 (0 -> 255)...")

    # 2. 產生漸層數據 (核心演算法)
    # np.linspace(0, 255, width): 在 0 到 255 之間產生 1920 個均勻分佈的浮點數
    # 例如: 0.0, 0.13, 0.26 ... 254.8, 255.0
    gradient_row = np.linspace(0, 255, width)

    # 3. 將這一行數據複製 height (1080) 次，形成二維矩陣
    # np.tile(array, (rows, cols))
    image_data = np.tile(gradient_row, (height, 1))

    # 4. 轉換數據格式
    # 將浮點數轉為 8-bit 整數 (uint8)，這是圖片像素的標準格式
    image_data = image_data.astype(np.uint8)

    # 5. 建立圖片物件
    # mode='L' 代表 8-bit 灰階圖
    img = Image.fromarray(image_data, mode='L')

    # 6. 轉為 RGB 模式 (24-bit)
    # 因為您之前的需求是 24-bit BMP，我們把單色灰階轉為 RGB 格式 (R=G=B)
    img_rgb = img.convert('RGB')

    # 7. 儲存檔案
    img_rgb.save(output_filename)
    print(f"已完成！圖片儲存為: {output_filename}")
    
    # 驗證一下最左邊和最右邊的數值
    print(f"驗證像素值:")
    print(f"  最左邊 (x=0): {img_rgb.getpixel((0, 500))}")
    print(f"  最右邊 (x=1919): {img_rgb.getpixel((1919, 500))}")

if __name__ == "__main__":
    create_grayscale_gradient_bmp()