# source venv/bin/activate

import numpy as np
from PIL import Image
import os

def compare_bitmaps(img1_path, img2_path):
    print(f"正在比對:\n  A: {img1_path}\n  B: {img2_path}")
    print("-" * 30)

    # 1. 檢查檔案是否存在
    if not os.path.exists(img1_path) or not os.path.exists(img2_path):
        print("錯誤：找不到指定的圖片檔案。")
        return

    try:
        # 2. 讀取圖片
        img1 = Image.open(img1_path)
        img2 = Image.open(img2_path)

        # 3. 檢查尺寸 (最快速的過濾方式)
        if img1.size != img2.size:
            print("結果：【尺寸不同】")
            print(f"  A: {img1.size}")
            print(f"  B: {img2.size}")
            return

        # 4. 檢查色彩模式 (例如 RGB vs RGBA vs 灰階)
        if img1.mode != img2.mode:
            print("結果：【色彩模式不同】")
            print(f"  A: {img1.mode}")
            print(f"  B: {img2.mode}")
            # 嘗試統一轉換為 RGB 繼續比對
            print(">> 嘗試轉換為 RGB 模式繼續比對...")
            img1 = img1.convert('RGB')
            img2 = img2.convert('RGB')

        # 5. 轉為 Numpy 陣列進行像素級比對
        arr1 = np.array(img1)
        arr2 = np.array(img2)

        # 使用 numpy 的 array_equal 進行全矩陣極速比對
        if np.array_equal(arr1, arr2):
            print("結果：【完全相同 (Identical)】")
            print("每一顆 Pixel 的數值都一模一樣。")
        else:
            print("結果：【發現差異 (Differences Found)】")
            
            # --- 進階分析：找出具體差異 ---
            # 算出差異矩陣
            diff_mask = (arr1 != arr2)
            
            # 統計有多少個數值不同 (針對 channel 計算)
            diff_count = np.sum(diff_mask)
            total_values = arr1.size
            
            print(f"  差異點數量: {diff_count} / {total_values} (數值點)")
            
            # 找出第一個差異點的座標
            # np.where 會回傳所有差異點的索引 (row, col, channel)
            diff_indices = np.where(diff_mask)
            
            if len(diff_indices[0]) > 0:
                first_idx = 0
                y = diff_indices[0][first_idx] # Row (Height)
                x = diff_indices[1][first_idx] # Col (Width)
                c = diff_indices[2][first_idx] if len(diff_indices) > 2 else 0 # Channel (RGB)
                
                val1 = arr1[y, x]
                val2 = arr2[y, x]
                
                # 判斷是單通道還是多通道
                if arr1.ndim == 3:
                    # RGB 模式
                    channel_map = {0: 'R', 1: 'G', 2: 'B'}
                    ch_name = channel_map.get(c, str(c))
                    print(f"  第一個差異範例:")
                    print(f"    座標: (x={x}, y={y})")
                    print(f"    通道: {ch_name}")
                    print(f"    數值: 圖A={val1[c]} vs 圖B={val2[c]}")
                else:
                    # 灰階模式
                    print(f"  第一個差異範例:")
                    print(f"    座標: (x={x}, y={y})")
                    print(f"    數值: 圖A={val1} vs 圖B={val2}")

    except Exception as e:
        print(f"發生未預期的錯誤: {e}")

if __name__ == "__main__":
    # 設定要比對的兩張圖
    file_a = "output_py_gradient.bmp"
    file_b = "output_c_gradient.bmp"
    
    compare_bitmaps(file_a, file_b)