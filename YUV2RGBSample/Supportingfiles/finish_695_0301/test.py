import numpy as np
from PIL import Image
import os
import time
from concurrent.futures import ThreadPoolExecutor

# ==========================================
# 核心運算邏輯 (維持不變)
# ==========================================
def solve_channel_vectorized(input_flat, refs_matrix, knots):
    """
    input_flat: 1D float32 array (Input Channel)
    refs_matrix: 5 x N float32 matrix (Reference Channels)
    knots: array [32, 64, 128, 192, 255]
    """
    bins = np.array([64, 128, 192])
    
    # 1. 快速找出每個 pixel 對應的區間索引
    seg_indices = np.searchsorted(bins, input_flat, side='right')
    
    # 2. 透過 Advanced Indexing 取得對應的 KNOTS 與 Refs 值
    x_low = knots[seg_indices]
    x_high = knots[seg_indices + 1]
    
    n_range = np.arange(len(input_flat))
    y_low = refs_matrix[seg_indices, n_range]
    y_high = refs_matrix[seg_indices + 1, n_range]
    
    # 3. 線性插值/外推計算
    slopes = (y_high - y_low) / (x_high - x_low)
    y_output = y_low + (input_flat - x_low) * slopes
    
    return y_output

# ==========================================
# I/O 讀取輔助函數 (簡化版)
# ==========================================
def load_full_rgb_worker(args):
    """
    讀取完整 RGB 圖片並回傳。
    args: (index, file_path)
    """
    idx, path = args
    try:
        with Image.open(path) as img:
            img = img.convert('RGB')
            # 回傳完整 RGB 陣列 (H, W, 3)，稍後再拆分通道
            return (idx, np.array(img))
    except Exception as e:
        print(f"讀取錯誤 {path}: {e}")
        return (idx, None)

# ==========================================
# 主程式 (修改處)
# ==========================================
def process_rgb_compensation_white_refs(input_path, white_refs_paths, output_path):
    # === 計時開始 ===
    t_start = time.perf_counter()
    print("=== 開始執行 RGB 補償 (使用 5 張白色參考圖) ===")

    KNOTS = np.array([32, 64, 128, 192, 255], dtype=np.float32)

    if len(white_refs_paths) != 5:
        print("錯誤：必須提供 5 張白色參考圖。")
        return

    # 1. 準備讀取任務列表
    # 我們只需要讀 6 個檔案：1 張 Input + 5 張 White Refs
    # ID 0: Input Image
    # ID 1-5: White Refs (對應 32, 64, 128, 192, 255)
    tasks = []
    tasks.append((0, input_path)) 
    
    for i, p in enumerate(white_refs_paths):
        tasks.append((1 + i, p))
    
    # 2. 平行讀取圖片
    t_io_start = time.perf_counter()
    print(f"正在同時讀取 {len(tasks)} 張圖片...")
    
    results_dict = {}
    # 只需要 6 個 worker，因為檔案數少
    with ThreadPoolExecutor(max_workers=6) as executor: 
        for result in executor.map(load_full_rgb_worker, tasks):
            idx, data = result
            if data is None:
                print("嚴重錯誤：讀取失敗，中止程序。")
                return
            results_dict[idx] = data
            
    t_io_end = time.perf_counter()
    print(f"  >> I/O 耗時: {t_io_end - t_io_start:.4f} 秒")

    # 3. 資料整理 (Data Preparation)
    t_prep_start = time.perf_counter()
    print("正在整理 RGB 通道矩陣...")

    # 取得 Input 圖資訊
    input_img = results_dict[0]
    h, w, _ = input_img.shape
    
    # 提取 Input 的三個通道 (攤平)
    in_r = input_img[:, :, 0].flatten().astype(np.float32)
    in_g = input_img[:, :, 1].flatten().astype(np.float32)
    in_b = input_img[:, :, 2].flatten().astype(np.float32)

    # 取得 5 張白色參考圖
    # results_dict[1] ~ results_dict[5] 是完整的 RGB array
    white_refs_list = [results_dict[i] for i in range(1, 6)]

    # --- 關鍵修改：從白色圖中分別拆出 R, G, B 矩陣 ---
    
    # 建立 R 參考矩陣: 取每一張白色圖的 Channel 0
    # Shape: (5, Total_Pixels)
    mat_refs_r = np.stack([img[:, :, 0].flatten().astype(np.float32) for img in white_refs_list])
    
    # 建立 G 參考矩陣: 取每一張白色圖的 Channel 1
    mat_refs_g = np.stack([img[:, :, 1].flatten().astype(np.float32) for img in white_refs_list])
    
    # 建立 B 參考矩陣: 取每一張白色圖的 Channel 2
    mat_refs_b = np.stack([img[:, :, 2].flatten().astype(np.float32) for img in white_refs_list])
    
    t_prep_end = time.perf_counter()
    print(f"  >> 資料整理耗時: {t_prep_end - t_prep_start:.4f} 秒")

    # 4. 分別執行向量化運算 (運算邏輯完全相同)
    t_calc_start = time.perf_counter()
    print("正在執行三通道向量化插值...")
    
    out_r = solve_channel_vectorized(in_r, mat_refs_r, KNOTS)
    out_g = solve_channel_vectorized(in_g, mat_refs_g, KNOTS)
    out_b = solve_channel_vectorized(in_b, mat_refs_b, KNOTS)
    
    t_calc_end = time.perf_counter()
    print(f"  >> 數學運算耗時: {t_calc_end - t_calc_start:.4f} 秒")

    # 5. 合成與存檔
    t_save_start = time.perf_counter()
    
    final_r = np.clip(np.round(out_r), 0, 255).astype(np.uint8).reshape(h, w)
    final_g = np.clip(np.round(out_g), 0, 255).astype(np.uint8).reshape(h, w)
    final_b = np.clip(np.round(out_b), 0, 255).astype(np.uint8).reshape(h, w)
    
    final_img_array = np.dstack((final_r, final_g, final_b))
    
    print(f"正在儲存: {output_path}")
    Image.fromarray(final_img_array, 'RGB').save(output_path)
    
    t_save_end = time.perf_counter()
    print(f"  >> 存檔耗時: {t_save_end - t_save_start:.4f} 秒")
    
    print(f"=== 總執行時間: {t_save_end - t_start:.4f} 秒 ===")

if __name__ == "__main__":
    # ================= 設定檔案路徑 =================
    
    # 1. 輸入圖
    input_file = "gradient.bmp"
    
    # 2. 白色補償圖 (請依序填入 32, 64, 128, 192, 255 對應的檔名)
    # 這些圖本身應該是白色的 (R=G=B)，但實際上程式會分別讀取它們的 R, G, B 通道
    white_refs = [
        "W32.bmp",
        "W64.bmp",
        "W128.bmp",
        "W192.bmp",
        "W255.bmp"
    ]

    output_file = "output_" + input_file
    
    # 檢查檔案
    all_files = [input_file] + white_refs
    
    if all(os.path.exists(f) for f in all_files):
        process_rgb_compensation_white_refs(
            input_file, 
            white_refs, 
            output_file
        )
    else:
        print("錯誤：部分檔案找不到，請檢查路徑。")
        missing = [f for f in all_files if not os.path.exists(f)]
        print("缺少的檔案:", missing)