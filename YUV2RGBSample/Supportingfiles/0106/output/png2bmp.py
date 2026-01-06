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
        if img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info):
            img = img.convert('RGBA')
            background = Image.new("RGB", img.size, (255, 255, 255))
            background.paste(img, mask=img.split()[3])
            img = background
        else:
            img = img.convert("RGB")

        # 4. 儲存為 BMP
        img.save(output_path)
        print(f"  -> 成功！")

    except Exception as e:
        print(f"  -> 失敗: {e}")

if __name__ == "__main__":
    # 設定目標資料夾 ('.' 代表目前程式所在的資料夾)
    target_folder = "."
    
    # 取得資料夾內所有檔案
    all_files = os.listdir(target_folder)
    
    # 過濾出所有 PNG 檔案 (不分大小寫)
    png_files = [f for f in all_files if f.lower().endswith(".png")]

    if not png_files:
        print("未在資料夾中發現任何 PNG 檔案。")
    else:
        print(f"發現 {len(png_files)} 個 PNG 檔案，開始轉換...\n")
        
        for filename in png_files:
            # 組合完整的檔案路徑
            full_path = os.path.join(target_folder, filename)
            convert_png_to_bmp(full_path)
            
        print("\n所有轉換作業完成。")