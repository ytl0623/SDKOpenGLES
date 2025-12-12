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

from PIL import Image

# 定義藍色亮度值 (對應 B 通道)
blue_values = [20, 50, 100, 150, 220]

# 圖片尺寸
width, height = 1920, 1080

print(f"開始產生 {width}x{height} 的藍色階調圖片...\n")

for val in blue_values:
    # 設定顏色: Red=0, Green=0, Blue=val
    # val 越小越暗(深藍)，255 為最亮(純藍)
    color = (val, 0, 0)
    
    # 創建圖片 (RGB 模式自動存為 24-bit)
    img = Image.new('RGB', (width, height), color=color)
    
    # 儲存為 BMP
    filename = f'red_{val}.bmp'
    img.save(filename, 'BMP')
    
    print(f'已產生: {filename} (RGB數值: {color})')

print('\n所有圖片已產生完成！')

# 產生灰階圖
# import numpy as np
# from PIL import Image

# def create_gradient_bmp(color_mode='white'):
#     """
#     產生指定顏色的 1920x1080 漸層圖 (0 -> 255)
    
#     Args:
#         color_mode (str): 選擇顏色模式 - 'red', 'green', 'blue', 'white'
#     """
    
#     # 1. 基本設定
#     width, height = 1920, 1080
#     output_filename = f'gradient_{color_mode}.bmp'
    
#     print(f"[{color_mode.upper()}] 正在生成 {width}x{height} 漸層圖...")

#     # 2. 準備基礎數據
#     # 漸層數據 (0 -> 255)
#     gradient_row = np.linspace(0, 255, width)
#     full_gradient = np.tile(gradient_row, (height, 1)).astype(np.uint8)
    
#     # 全黑數據 (0)
#     full_zeros = np.zeros((height, width), dtype=np.uint8)

#     # 3. 根據顏色模式分配 R, G, B 通道
#     if color_mode == 'red':
#         r, g, b = full_gradient, full_zeros, full_zeros
#     elif color_mode == 'green':
#         r, g, b = full_zeros, full_gradient, full_zeros
#     elif color_mode == 'blue':
#         r, g, b = full_zeros, full_zeros, full_gradient
#     elif color_mode == 'white':
#         # 白色即為標準灰階 (R=G=B)
#         r, g, b = full_gradient, full_gradient, full_gradient
#     else:
#         print("錯誤: 不支援的顏色模式，請使用 red, green, blue 或 white")
#         return

#     # 4. 合併通道 (Stack)
#     # 組合出 (Height, Width, 3) 的矩陣
#     rgb_data = np.dstack((r, g, b))

#     # 5. 建立圖片並儲存
#     img = Image.fromarray(rgb_data, mode='RGB')
#     img.save(output_filename)
    
#     print(f" -> 已儲存為: {output_filename}")
    
#     # 簡單驗證中間點的像素值
#     mid_x = width // 2
#     mid_pixel = img.getpixel((mid_x, 500))
#     print(f" -> 中間點像素值 (x={mid_x}): {mid_pixel}\n")

# if __name__ == "__main__":
#     # 您可以在這裡修改要產生的顏色
#     # create_gradient_bmp('red')
#     # create_gradient_bmp('green')
#     # create_gradient_bmp('blue')
#     # create_gradient_bmp('white')

#     # 或者一次產生全部四種圖：
#     colors = ['red', 'green', 'blue', 'white']
#     for c in colors:
#         create_gradient_bmp(c)