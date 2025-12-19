import os
from PIL import Image

def convert_png_to_bmp(png_path, output_path=None):
    try:
        # 1. 開啟圖片
        img = Image.open(png_path)
        
        # 2. 處理輸出檔名
        if output_path is None:
            # 如果沒指定輸出檔名，就用原本的檔名，只是把副檔名改成 .bmp
            output_path = os.path.splitext(png_path)[0] + ".bmp"

        print(f"正在轉換: {png_path} -> {output_path}")

        # 3. 處理透明度 (Alpha Channel)
        # 如果圖片是 RGBA (有透明度) 或 P (調色盤模式)，直接轉 RGB 會讓透明變黑
        # 我們創建一個白色背景來合成
        if img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info):
            # 必須先轉為 RGBA 確保有 Alpha 通道
            img = img.convert('RGBA')
            # 建立一個全白的背景
            background = Image.new("RGB", img.size, (255, 255, 255))
            # 將原本的圖貼到白背景上，並使用原本的 Alpha 當作遮罩
            background.paste(img, mask=img.split()[3])
            img = background
        else:
            # 如果原本就沒有透明度，直接轉 RGB 確保格式正確 (24-bit)
            img = img.convert("RGB")

        # 4. 儲存為 BMP
        img.save(output_path)
        print("轉換成功！")

    except Exception as e:
        print(f"轉換失敗: {e}")

if __name__ == "__main__":
    # 在這裡修改你的檔名
    source_file = "r192.png" 
    
    convert_png_to_bmp(source_file)
