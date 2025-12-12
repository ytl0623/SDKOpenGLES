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

// 定義 KNOTS 常數 (維持不變，對應 5 張圖)
const float KNOTS[] = {32.0f, 64.0f, 128.0f, 192.0f, 255.0f};

// 輔助函數：限制數值範圍在 0-255
unsigned char clip(float val) {
    if (val < 0.0f) return 0;
    if (val > 255.0f) return 255;
    float rounded = nearbyintf(val); 
    return (unsigned char)rounded;
}

int main() {
    // ================= 設定檔案路徑 =================
    const char *input_file = "Supportingfiles/test/gradient_red.bmp";
    const char *output_file = "output_c_gradient_red.bmp";
    
    const char *white_refs_paths[] = {
        "Supportingfiles/135/Correction-Red-32.bmp",
        "Supportingfiles/135/Correction-Red-64.bmp",
        "Supportingfiles/135/Correction-Red-128.bmp",
        "Supportingfiles/135/Correction-Red-192.bmp",
        "Supportingfiles/135/Correction-Red-255.bmp"
    };
    const int num_refs = 5;

    // 計時開始
    double t_start = omp_get_wtime();
    printf("=== 開始執行 RGB 補償 (C語言 OpenMP版 - 含原點修正) ===\n");

    // 1. 讀取 Input 圖片
    int width, height, channels;
    printf("正在讀取輸入圖: %s\n", input_file);
    unsigned char *input_data = stbi_load(input_file, &width, &height, &channels, 3);
    
    if (!input_data) {
        printf("錯誤：無法讀取輸入圖 %s\n", input_file);
        return 1;
    }

    if (channels != 3 && channels != 4) {
        printf("警告：輸入圖片通道數異常 (%d)，已強制轉為 RGB。\n", channels);
    }
    
    size_t total_elements = (size_t)width * (size_t)height * 3;

    // 2. 讀取 Reference 圖片
    unsigned char *ref_data[5];
    
    for (int i = 0; i < num_refs; i++) {
        int w, h, c;
        ref_data[i] = stbi_load(white_refs_paths[i], &w, &h, &c, 3);
        
        if (!ref_data[i]) {
            printf("錯誤：無法讀取參考圖 %s\n", white_refs_paths[i]);
            free(input_data);
            for(int j=0; j<i; j++) free(ref_data[j]);
            return 1;
        }
        
        if (w != width || h != height) {
            printf("錯誤：參考圖尺寸不符。\n");
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
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < total_elements; i++) {
        float val = (float)input_data[i];
        
        float x_low, x_high, y_low, y_high;

        // =========================================================
        // 修改處：加入原點 (0,0) 的錨定邏輯
        // =========================================================
        if (val < 32.0f) {
            // [區間 0] 0 -> 32
            // 強制設定起點為原點 (0, 0)
            x_low = 0.0f;
            y_low = 0.0f;
            
            // 終點為第一張參考圖 (對應 32)
            x_high = KNOTS[0];            // 32.0f
            y_high = (float)ref_data[0][i];
        } 
        else {
            // [區間 1~4] 32 -> 255
            // 維持原本的 search sorted 邏輯
            int idx;
            if (val < 64.0f) idx = 0;       // 32 -> 64
            else if (val < 128.0f) idx = 1; // 64 -> 128
            else if (val < 192.0f) idx = 2; // 128 -> 192
            else idx = 3;                   // 192 -> 255

            x_low = KNOTS[idx];
            x_high = KNOTS[idx + 1];
            y_low = (float)ref_data[idx][i];
            y_high = (float)ref_data[idx + 1][i];
        }

        // 線性插值計算
        // 防止除以零 (雖然理論上 KNOTS 都有間隔，但為了安全)
        float denominator = x_high - x_low;
        if (denominator == 0.0f) {
            output_data[i] = clip(y_high);
        } else {
            float slope = (y_high - y_low) / denominator;
            float result = y_low + (val - x_low) * slope;
            output_data[i] = clip(result);
        }
    }

    double t_calc_end = omp_get_wtime();
    printf(" >> 數學運算耗時: %.4f 秒\n", t_calc_end - t_calc_start);

    // 5. 存檔
    printf("正在儲存: %s\n", output_file);
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