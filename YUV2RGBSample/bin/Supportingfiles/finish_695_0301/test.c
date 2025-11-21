// gcc -O3 -fopenmp test.c -lm
// ./a.out

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <fenv.h>

// 引入 STB 圖像處理庫
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// OpenMP 用於平行運算
#include <omp.h>

// 定義 KNOTS 常數
const float KNOTS[] = {32.0f, 64.0f, 128.0f, 192.0f, 255.0f};

// 輔助函數：限制數值範圍在 0-255
unsigned char clip(float val) {
    // 限制範圍
    if (val < 0.0f) return 0;
    if (val > 255.0f) return 255;
    
    // 銀行家捨入法 (Round half to even)
    // 注意：這比單純 +0.5 慢一點，但跟 NumPy 結果會一致
    // 需要編譯時連結 -lm
    float rounded = nearbyintf(val); 
    
    return (unsigned char)rounded;
}

int main() {
    // ================= 設定檔案路徑 =================
    const char *input_file = "gradient.bmp";
    const char *output_file = "output_c_gradient.bmp";
    
    const char *white_refs_paths[] = {
        "W32.bmp",
        "W64.bmp",
        "W128.bmp",
        "W192.bmp",
        "W255.bmp"
    };
    const int num_refs = 5;

    // 計時開始
    double t_start = omp_get_wtime();
    printf("=== 開始執行 RGB 補償 (C語言 OpenMP版) ===\n");

    // 1. 讀取 Input 圖片
    int width, height, channels;
    printf("正在讀取輸入圖: %s\n", input_file);
    unsigned char *input_data = stbi_load(input_file, &width, &height, &channels, 3); // 強制讀取為 3 channels (RGB)
    
    if (!input_data) {
        printf("錯誤：無法讀取輸入圖 %s\n", input_file);
        return 1;
    }

    // 檢查格式
    if (channels != 3 && channels != 4) {
        printf("警告：輸入圖片通道數異常 (%d)，已強制轉為 RGB。\n", channels);
    }
    
    // 計算總像素數據大小 (Width * Height * 3)
    // 注意：我們不需要像 Python 那樣拆分 R/G/B，直接把整張圖當作一個巨大的 float 陣列處理即可
    // 因為 Input 的 R 對應 Ref 的 R，位置索引完全一致。
    size_t total_elements = (size_t)width * (size_t)height * 3;

    // 2. 讀取 Reference 圖片
    unsigned char *ref_data[5]; // 儲存 5 張參考圖的指標
    
    for (int i = 0; i < num_refs; i++) {
        int w, h, c;
        ref_data[i] = stbi_load(white_refs_paths[i], &w, &h, &c, 3);
        
        if (!ref_data[i]) {
            printf("錯誤：無法讀取參考圖 %s\n", white_refs_paths[i]);
            // 清理已分配記憶體
            free(input_data);
            for(int j=0; j<i; j++) free(ref_data[j]);
            return 1;
        }
        
        if (w != width || h != height) {
            printf("錯誤：參考圖 %s 尺寸 (%dx%d) 與輸入圖 (%dx%d) 不符。\n", 
                   white_refs_paths[i], w, h, width, height);
            return 1;
        }
    }
    
    double t_io_end = omp_get_wtime();
    printf(" >> I/O 與準備耗時: %.4f 秒\n", t_io_end - t_start);

    // 3. 準備輸出緩衝區
    unsigned char *output_data = (unsigned char *)malloc(total_elements);
    if (!output_data) {
        printf("記憶體配置失敗。\n");
        return 1;
    }

    printf("正在執行向量化插值運算 (OpenMP)... Total elements: %zu\n", total_elements);
    double t_calc_start = omp_get_wtime();

    // 4. 核心運算迴圈 (OpenMP 平行化)
    // 這裡取代了 Python 的 solve_channel_vectorized
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < total_elements; i++) {
        float val = (float)input_data[i];
        
        // 對應 Python: seg_indices = np.searchsorted(bins, input_flat, side='right')
        // bins = [64, 128, 192]
        // side='right' 意味著:
        // input < 64   -> idx 0
        // input >= 64  -> idx 1
        // input >= 128 -> idx 2
        // input >= 192 -> idx 3
        
        int idx;
        if (val < 64.0f) idx = 0;
        else if (val < 128.0f) idx = 1;
        else if (val < 192.0f) idx = 2;
        else idx = 3;

        // 取得 Knot (X軸)
        float x_low = KNOTS[idx];
        float x_high = KNOTS[idx + 1];

        // 取得 Ref (Y軸)
        // ref_data[idx][i] 對應 y_low
        // ref_data[idx+1][i] 對應 y_high
        float y_low = (float)ref_data[idx][i];
        float y_high = (float)ref_data[idx + 1][i];

        // 線性插值
        float slope = (y_high - y_low) / (x_high - x_low);
        float result = y_low + (val - x_low) * slope;

        // 存回輸出陣列
        output_data[i] = clip(result);
    }

    double t_calc_end = omp_get_wtime();
    printf(" >> 數學運算耗時: %.4f 秒\n", t_calc_end - t_calc_start);

    // 5. 存檔
    printf("正在儲存: %s\n", output_file);
    // stbi_write_bmp(檔名, 寬, 高, 通道數, 資料指標)
    stbi_write_bmp(output_file, width, height, 3, output_data);

    double t_save_end = omp_get_wtime();
    printf(" >> 存檔耗時: %.4f 秒\n", t_save_end - t_calc_end);
    printf("=== 總執行時間: %.4f 秒 ===\n", t_save_end - t_start);

    // 6. 清理記憶體
    stbi_image_free(input_data);
    for (int i = 0; i < num_refs; i++) {
        stbi_image_free(ref_data[i]);
    }
    free(output_data);

    return 0;
}