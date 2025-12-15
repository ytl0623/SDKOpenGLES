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
 * @file XGLSLCompile.h
 *
 * 此檔案實作了所有與 GLSL (OpenGL著色語言) 編譯相關的功能。
 * (This file implements all the functions related to GLSL compilation.)
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


#ifndef XGLSLCOMPILE_H
#define XGLSLCOMPILE_H

#include <GLES2/gl2.h>

/**
 * @class Shader
 * @brief 負責讀取、編譯與處理 GLSL 著色器的類別。
 */
class Shader
{
private:
    /**
     * @brief 讀取著色器原始碼檔案。
     * * @param filename 著色器檔案的路徑。
     * @return char* 回傳包含著色器原始碼的字串指標。
     */
    static char *loadShader(const char *filename);

public:
    /**
     * @brief 處理並編譯著色器。
     * 此函式會呼叫 loadShader 讀取檔案，接著建立並編譯著色器物件。
     * * @param shader 指向 GLuint 的指標，用於存放編譯成功後的著色器 ID (Handle)。
     * @param filename 著色器原始碼檔案的路徑。
     * @param shaderType 著色器類型 (例如 GL_VERTEX_SHADER 或 GL_FRAGMENT_SHADER)。
     */
    static void processShader(GLuint *shader, const char *filename, GLint shaderType);
};

#endif /* XGLSLCOMPILE_H */