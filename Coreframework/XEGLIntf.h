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
 * @file XEGLIntf.h
 *
 * 此檔案實作了所有與 EGL 綁定相關的功能。
 * (This file implements all the functions related to EGL bindings.)
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
/******************************* Header Files ********************************/

#ifndef XEGLINTF_H
#define XEGLINTF_H

#include <EGL/egl.h>
#include <EGL/eglext.h>

/**
 * @class CoreEGL
 * @brief 負責管理 EGL 的生命週期、配置與上下文環境。
 */
class CoreEGL
{
private:
    /**
     * @brief 尋找合適的 EGL 配置 (Config)。
     * @param strictMatch 是否需要嚴格匹配屬性。
     * @return EGLConfig 回傳找到的 EGL 配置。
     */
    static EGLConfig findConfig(bool strictMatch);

    static EGLint configAttributes[];  // EGL 配置屬性陣列 (如顏色深度、緩衝區大小等)
    static EGLint contextAttributes[]; // EGL 上下文屬性陣列 (如 OpenGL ES 版本)
    static EGLint windowAttributes[];  // EGL 視窗屬性陣列

public:
    /**
     * @brief 設定 EGL 的多重取樣 (Multisampling/Anti-aliasing) 數量。
     * @param requiredEGLSamples 需要的樣本數 (例如 4 代表 4x MSAA)。
     */
    static void setEGLSamples(EGLint requiredEGLSamples);

    /**
     * @enum OpenGLESVersion
     * @brief 定義支援的 OpenGL ES API 版本。
     */
    enum OpenGLESVersion {
        OPENGLES1, 
        OPENGLES2, 
        OPENGLES3, 
        OPENGLES31
    };

    // 靜態成員變數，用於儲存 EGL 的核心物件
    static EGLDisplay display; // EGL 顯示連接 (Display connection)
    static EGLContext context; // EGL 渲染上下文 (Rendering context)
    static EGLConfig config;   // EGL 幀緩衝配置 (Frame buffer configuration)
    static EGLSurface surface; // EGL 繪圖表面 (Drawing surface)

    /**
     * @brief 初始化 EGL 環境。
     * 建立 Display、Surface 與 Context。
     * @param requestedAPIVersion 請求使用的 OpenGL ES 版本。
     */
    static void initializeEGL(OpenGLESVersion requestedAPIVersion);

    /**
     * @brief 終止 EGL 並釋放相關資源。
     * 銷毀 Context、Surface 並終止 Display 連接。
     */
    static void terminateEGL(void);
};

#endif /* XEGLINTF_H */