#!/bin/bash

# 顯示目前正在執行的動作
echo "正在切換到專案目錄..."

# 切換到目標目錄
# 注意：這裡假設 SDKOpenGLES 位於您的家目錄 (~) 下
# 如果該資料夾在桌面，請改為: cd ~/Desktop/SDKOpenGLES/YUV2RGBSample/
cd ~/SDKOpenGLES/YUV2RGBSample/ || { echo "找不到目錄，請檢查路徑"; exit 1; }

echo "設定環境變數..."
# 載入上層目錄的 export 設定
source ../run_exports.sh

# 設定顯示輸出
export DISPLAY=:1

echo "清理並重新編譯專案..."
# 清除舊的編譯檔案cd 
make clean

# 編譯專案
# 如果 make 失敗，腳本將會停止，不會執行程式
make || { echo "編譯失敗"; exit 1; }

echo "執行應用程式..."
# 執行程式並帶入參數
# ./YUV2RGBSample Supportingfiles/blue.bmp gray_90.bmp gray_150.bmp gray_180.bmp gray_200.bmp gray_250.bmp
./YUV2RGBSample Supportingfiles/blue_128.bmp Supportingfiles/finish_695_0301/Correction-Blue-32.bmp Supportingfiles/finish_695_0301/Correction-Blue-64.bmp Supportingfiles/finish_695_0301/Correction-Blue-128.bmp Supportingfiles/finish_695_0301/Correction-Blue-192.bmp Supportingfiles/finish_695_0301/Correction-Blue-255.bmp

./YUV2RGBSample Supportingfiles/4763824-hd_1920_1080_24fps.mp4 Supportingfiles/finish_695_0301/W32.bmp Supportingfiles/finish_695_0301/W64.bmp Supportingfiles/finish_695_0301/W128.bmp Supportingfiles/finish_695_0301/W192.bmp Supportingfiles/finish_695_0301/W255.bmp


echo "執行完成。"
