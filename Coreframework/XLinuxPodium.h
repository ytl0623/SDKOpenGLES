/*******************************************************************************
 *
 * Copyright (C) 2023 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Use of the Software is limited solely to applications:
 * (a) running on a Xilinx device, or
 * (b) that interact with a Xilinx device through a bus or interconnect.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * XILINX CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 *
*******************************************************************************/
/******************************************************************************/
/**
 *
 * @file XLinuxPodium.h
 *
 * 此檔案實作了應用程式所需的 Linux X11 視窗系統 API 相關功能。
 * (This file implements all the functions related to Linux X11 windowing system API's for application.)
 *
 * @note        None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who            Date            Changes
 * ----- ----           --------        -----------------------------------------------
 * 1.0   Alok G         10/06/17        Initial release.
 * </pre>
 *
*******************************************************************************/
/******************************* Source Files ********************************/



#ifndef XLINUXPODIUM_H
#define XLINUXPODIUM_H

#include <cstdlib>
#include <EGL/egl.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "XPodium.h"

/**
 * @class XLinuxPodium
 * @brief 繼承自 XPodium，專門用於 Linux X11 環境下的視窗管理類別。
 * 實作了單例模式 (Singleton Pattern)。
 */
class XLinuxPodium : public XPodium
{
private:
    int windowWidth;        // 視窗寬度
    int windowHeight;       // 視窗高度
    Colormap colormap;      // X11 顏色映射表 (Color Map)
    XVisualInfo *visual;    // X11 視覺資訊結構 (用於匹配 EGL 設定)

    static XPodium* instance; // 單例模式的靜態實體指標

    /**
     * @brief 私有建構子，防止外部直接實例化。
     */
    XLinuxPodium(void);

    /**
     * @brief 等待 X11 視窗映射 (Map) 到螢幕上的回呼函式。
     * 用於確保視窗在進行繪圖前已經準備好。
     * @param display X11 顯示連接
     * @param event X11 事件結構
     * @param windowPointer 自定義參數 (通常指向視窗 ID)
     * @return Bool 如果事件符合條件則回傳 True
     */
    static Bool wait_for_map(Display *display, XEvent *event, char *windowPointer);

public:
    /**
     * @brief 取得 XLinuxPodium 的單例實體。
     * @return XPodium* 指向唯一實體的指標。
     */
    static XPodium* getHandler(void);

    /**
     * @brief 準備視窗參數 (設定寬高)。
     * @param width 寬度
     * @param height 高度
     */
    virtual void prepareWindow(int width, int height);

    /**
     * @brief 銷毀 X11 視窗並釋放資源。
     */
    virtual void destroyWindow(void);

    /**
     * @brief 檢查視窗狀態。
     * 通常用於處理 X11 事件迴圈 (Event Loop)，如按鍵或關閉視窗訊號。
     * @return WindowStatus 目前視窗的狀態。
     */
    virtual WindowStatus checkWindow(void);

    /**
     * @brief 實際建立 X11 視窗的函式。
     * 包含建立 Display 連接、選擇 Visual、建立 Colormap 與 Window。
     * @return bool 建立成功回傳 true，失敗回傳 false。
     */
    bool createX11Window(void);
};

#endif /* XLINUXPODIUM_H */