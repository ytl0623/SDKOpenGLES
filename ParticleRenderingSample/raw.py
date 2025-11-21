import numpy as np
import matplotlib.pyplot as plt
import argparse

def load_raw_image(file_path, width, height):
    """
    讀取 .raw 灰階圖像檔案。
    :param file_path: 檔案路徑
    :param width: 圖像寬度
    :param height: 圖像高度
    :return: numpy 陣列 (height, width)
    """
    num_bytes = width * height
    with open(file_path, 'rb') as f:
        data = np.fromfile(f, dtype=np.uint8, count=num_bytes)
    
    if len(data) != num_bytes:
        raise ValueError(f"檔案大小不匹配：預期 {num_bytes} bytes，但讀取到 {len(data)} bytes")
    
    image = data.reshape((height, width))
    return image

def display_raw_image(file_path, width, height):
    """
    顯示 .raw 灰階圖像。
    """
    image = load_raw_image(file_path, width, height)
    plt.figure(figsize=(8, 8))
    plt.imshow(image, cmap='gray')
    plt.title(f'RAW 灰階圖像: {file_path}')
    plt.axis('off')
    plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='顯示 .raw 灰階圖像檔案')
    parser.add_argument('file_path', type=str, help='RAW 檔案路徑 (e.g., texture.raw)')
    parser.add_argument('width', type=int, help='圖像寬度 (e.g., 64)')
    parser.add_argument('height', type=int, help='圖像高度 (e.g., 64)')
    
    args = parser.parse_args()
    display_raw_image(args.file_path, args.width, args.height)
