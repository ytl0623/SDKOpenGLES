import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import subprocess
import os
import sys

# ==========================================
# 請在此修改您的實際執行檔名稱/路徑
# ==========================================
BINARY_PHOTO = "./Photo"  # Photo 模式執行的程式
BINARY_VIDEO = "./Video"  # Video 模式執行的程式
# ==========================================

# 定義需要的灰階階數
GRAY_LEVELS = [32, 64, 128, 192, 224]

class YUVConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("GPU Demura")
        self.root.geometry("650x750") # 高度加大以容納5個獨立選項
        
        # 用字典來儲存這5個 Entry 物件，方便後續讀取
        # Key: 灰階值 (int), Value: Entry widget
        self.gray_entries = {} 

        # --- 0. 模式選擇 ---
        self.frame_mode = ttk.LabelFrame(root, text="0. 選擇執行模式")
        self.frame_mode.pack(padx=10, pady=5, fill="x")

        self.mode_var = tk.StringVar(value="photo") 

        self.rb_photo = ttk.Radiobutton(
            self.frame_mode, text="Photo 模式", variable=self.mode_var, value="photo"
        )
        self.rb_photo.pack(side="left", padx=20, pady=10)

        self.rb_video = ttk.Radiobutton(
            self.frame_mode, text="Video 模式", variable=self.mode_var, value="video"
        )
        self.rb_video.pack(side="left", padx=20, pady=10)


        # --- 1. 主要輸入檔案 ---
        self.frame_main = ttk.LabelFrame(root, text="1. 主要輸入檔案 (Input File)")
        self.frame_main.pack(padx=10, pady=5, fill="x")
        
        self.entry_main = ttk.Entry(self.frame_main)
        self.entry_main.pack(side="left", padx=5, pady=10, fill="x", expand=True)
        
        btn_main = ttk.Button(self.frame_main, text="瀏覽", command=self.browse_main)
        btn_main.pack(side="right", padx=5, pady=10)

        # --- 2. 補償圖設定 (獨立選取 5 階) ---
        self.frame_comps = ttk.LabelFrame(root, text="2. 補償圖設定 (請依序選擇 5 個灰階圖)")
        self.frame_comps.pack(padx=10, pady=5, fill="x")

        # 使用迴圈動態產生 5 個選取列
        for level in GRAY_LEVELS:
            self.create_gray_selector(self.frame_comps, level)

        # --- 3. 執行按鈕 ---
        self.btn_run = ttk.Button(root, text="開始執行轉換", command=self.run_process)
        self.btn_run.pack(pady=10, ipadx=40, ipady=8)

        # --- 4. 執行結果 Log ---
        self.frame_log = ttk.LabelFrame(root, text="3. 執行結果 Log")
        self.frame_log.pack(padx=10, pady=5, fill="both", expand=True)

        self.text_log = tk.Text(self.frame_log, bg="black", fg="lime", font=("Courier", 10))
        self.scrollbar = ttk.Scrollbar(self.frame_log, orient="vertical", command=self.text_log.yview)
        
        self.text_log.configure(yscrollcommand=self.scrollbar.set)
        
        self.scrollbar.pack(side="right", fill="y")
        self.text_log.pack(side="left", fill="both", expand=True)

    def create_gray_selector(self, parent, level):
        """ 建立單一灰階的選取列 """
        frame = ttk.Frame(parent)
        frame.pack(fill="x", padx=5, pady=2)

        # 標籤 (固定寬度讓介面整齊)
        lbl = ttk.Label(frame, text=f"Gray {level}:", width=10, anchor="e")
        lbl.pack(side="left", padx=(5, 10))

        # 輸入框
        entry = ttk.Entry(frame)
        entry.pack(side="left", fill="x", expand=True, padx=5)
        
        # 存入字典，Key 為灰階數值
        self.gray_entries[level] = entry

        # 瀏覽按鈕 (使用 lambda 綁定目前的 level)
        btn = ttk.Button(frame, text="瀏覽", command=lambda l=level: self.browse_single_comp(l))
        btn.pack(side="right", padx=5)

    def browse_main(self):
        filename = filedialog.askopenfilename(
            filetypes=[("BMP Files", "*.bmp"), ("Video Files", "*.mp4;*.avi;*.mkv"), ("All Files", "*.*")]
        )
        if filename:
            self.entry_main.delete(0, tk.END)
            self.entry_main.insert(0, filename)

    def browse_single_comp(self, level):
        """ 針對特定的灰階值選取檔案 """
        filename = filedialog.askopenfilename(
            title=f"請選擇 Gray {level} 的補償圖",
            filetypes=[("BMP Files", "*.bmp"), ("All Files", "*.*")]
        )
        if filename:
            entry_widget = self.gray_entries[level]
            entry_widget.delete(0, tk.END)
            entry_widget.insert(0, filename)
            # 自動捲動到最後，方便看到檔名
            entry_widget.xview_moveto(1)

    def log(self, msg, tag=None):
        self.text_log.insert(tk.END, msg + "\n", tag)
        self.text_log.see(tk.END)

    def run_process(self):
        self.text_log.delete(1.0, tk.END)
        
        # 1. 決定模式
        current_mode = self.mode_var.get()
        if current_mode == "photo":
            target_binary = BINARY_PHOTO
            mode_name = "Photo"
        else:
            target_binary = BINARY_VIDEO
            mode_name = "Video"

        self.log(f">>> 準備開始執行 ({mode_name} 模式)...", "info")

        # 2. 檢查主檔
        main_input = self.entry_main.get().strip()
        if not main_input:
            self.log("[錯誤] 未選擇主要輸入檔案！", "error")
            return

        # 3. 收集並檢查 5 張補償圖
        comp_files_sorted = []
        missing_levels = []

        # 依照 GRAY_LEVELS 定義的順序 (32 -> 64 -> 128 -> 192 -> 224) 抓取路徑
        for level in GRAY_LEVELS:
            path = self.gray_entries[level].get().strip()
            if not path:
                missing_levels.append(str(level))
            else:
                comp_files_sorted.append(path)

        if missing_levels:
            self.log(f"[錯誤] 以下灰階補償圖未選取: {', '.join(missing_levels)}", "error")
            return

        # 4. 檢查執行檔
        if not os.path.exists(target_binary):
            self.log(f"[錯誤] 找不到執行檔: {target_binary}", "error")
            return

        # 5. 組合指令
        # 參數順序: [執行檔] [主檔] [G32] [G64] [G128] [G192] [G224]
        cmd = [target_binary, main_input] + comp_files_sorted
        
        self.log("正在執行...")
        # 為了不讓 Log 太多，這裡只顯示檔名而非全路徑 (Optional)
        debug_cmd_display = [os.path.basename(c) for c in cmd]
        self.log(f"CMD參數順序: {debug_cmd_display}", "info")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True)
            
            if result.stdout:
                self.log("--- STDOUT ---")
                self.log(result.stdout)
            if result.stderr:
                self.log("--- STDERR ---")
                self.log(result.stderr)
            
            self.log("-" * 30)
            if result.returncode == 0:
                self.log(f">>> {mode_name} 執行成功", "success")
            else:
                self.log(f">>> {mode_name} 執行失敗 (Code: {result.returncode})", "error")

        except Exception as e:
            self.log(f"[系統例外錯誤]: {str(e)}", "error")

if __name__ == "__main__":
    root = tk.Tk()
    app = YUVConverterApp(root)
    
    app.text_log.tag_config("error", foreground="red")
    app.text_log.tag_config("success", foreground="cyan")
    app.text_log.tag_config("info", foreground="yellow")
    
    root.mainloop()