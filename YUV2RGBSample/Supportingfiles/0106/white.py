import cv2
import os

def combine_rgb_channels():
    # 定義要處理的灰階值列表
    levels = [64, 95, 128, 156]
    
    # 定義輸入檔案的前綴與對應的 OpenCV BGR 通道索引
    # OpenCV 讀取的順序是 BGR: 0=Blue, 1=Green, 2=Red
    channel_map = {
        'Blue': 0,
        'Green': 1,
        'Red': 2
    }

    print("開始合併圖片...")

    for level in levels:
        try:
            # 1. 建構檔名
            b_file = f"Blue{level}.Bmp"
            g_file = f"Green{level}.Bmp"
            r_file = f"Red{level}.Bmp"
            output_file = f"White{level}.Bmp"

            # 檢查檔案是否存在
            if not (os.path.exists(b_file) and os.path.exists(g_file) and os.path.exists(r_file)):
                print(f"[跳過] 找不到等級 {level} 的部分原始檔案")
                continue

            # 2. 讀取圖片 (保持原色讀取)
            img_b_src = cv2.imread(b_file)
            img_g_src = cv2.imread(g_file)
            img_r_src = cv2.imread(r_file)

            # 檢查圖片大小是否一致
            if not (img_b_src.shape == img_g_src.shape == img_r_src.shape):
                print(f"[錯誤] 等級 {level} 的圖片尺寸不一致")
                continue

            # 3. 提取對應的單一通道
            # 從 Blue 檔案提取 Blue Channel (0)
            b_channel = img_b_src[:, :, 0]
            # 從 Green 檔案提取 Green Channel (1)
            g_channel = img_g_src[:, :, 1]
            # 從 Red 檔案提取 Red Channel (2)
            r_channel = img_r_src[:, :, 2]

            # 4. 合併通道 (Merge) 順序為 B, G, R
            white_img = cv2.merge([b_channel, g_channel, r_channel])

            # 5. 存檔
            cv2.imwrite(output_file, white_img)
            print(f"[成功] 已建立: {output_file}")

        except Exception as e:
            print(f"[例外] 處理等級 {level} 時發生錯誤: {e}")

    print("處理完成。")

if __name__ == "__main__":
    combine_rgb_channels()
