import pandas as pd
import matplotlib.pyplot as plt
import os
import numpy as np

# =========================
# CONFIG
# =========================
FILE_PATH = "sweep_video_metrics.xlsx"
OUTPUT_DIR = "plots"
if not os.path.exists(OUTPUT_DIR):
    os.makedirs(OUTPUT_DIR)

def plot_eel_wave_static(df, video_name):
    """
    將 6 個紅點表示得像駐波/行進波一樣 (空間域分析)
    """
    plt.figure(figsize=(12, 6), dpi=100)
    
    # 提取所有時間點
    frames = df['frame_no'].unique()
    
    # 為了讓畫面乾淨，我們每隔幾幀取一個樣（例如每 0.5 秒取一條波形）
    # 這樣多條曲線重疊在一起，就會形成像「駐波包絡線」的感覺
    sample_stride = 10 
    
    # 使用漸層色代表時間演進
    colors = plt.cm.Blues(np.linspace(0.3, 1, len(frames[::sample_stride])))
    
    x_nodes = np.arange(1, 7) # 代表 6 個節點的位置 (1=頭, 6=尾)

    for idx, f_no in enumerate(frames[::sample_stride]):
        frame_data = df[df['frame_no'] == f_no]
        if len(frame_data) == 1:
            # 抓取該時刻 6 個點的 dev_y
            y_values = [frame_data[f'pt{i}_dev_y'].values[0] for i in range(1, 7)]
            
            # 繪製該瞬間的身體曲線
            plt.plot(x_nodes, y_values, color=colors[idx], alpha=0.3, linewidth=1)
            # 在節點處點上小點
            plt.scatter(x_nodes, y_values, color=colors[idx], s=10, alpha=0.2)

    # 繪製最核心的一條線 (最後一幀) 做強化
    last_frame = df[df['frame_no'] == frames[-1]]
    last_y = [last_frame[f'pt{i}_dev_y'].values[0] for i in range(1, 7)]
    plt.plot(x_nodes, last_y, color='navy', linewidth=2.5, label='Current Body Shape')
    plt.scatter(x_nodes, last_y, color='red', s=50, zorder=5, label='Red Points (Sensors)')

    # --- 視覺美化 ---
    plt.axhline(0, color='black', linewidth=0.8, linestyle='--')
    plt.title(f"Traveling Wave Envelope - {video_name}", fontsize=14, fontweight='bold')
    plt.xlabel("Segment Node (Head -> Tail)", fontsize=12)
    plt.ylabel("Lateral Deviation (px)", fontsize=12)
    plt.xticks(x_nodes, [f'pt{i}' for i in x_nodes])
    plt.grid(True, axis='y', linestyle=':', alpha=0.6)
    plt.legend()
    
    # 限制 Y 軸範圍讓波形更明顯
    max_amp = df[[f'pt{i}_dev_y' for i in range(1, 7)]].abs().max().max()
    plt.ylim(-max_amp*1.2, max_amp*1.2)

    plt.tight_layout()
    save_path = os.path.join(OUTPUT_DIR, f"{video_name}_standing_wave.png")
    plt.savefig(save_path)
    print(f"駐波圖表已儲存: {save_path}")
    plt.show()

def main():
    print("正在讀取數據...")
    try:
        df_all = pd.read_excel(FILE_PATH, sheet_name='tracks')
    except Exception as e:
        print(f"讀取失敗: {e}")
        return

    video_files = df_all['meta_file'].unique()
    
    if len(video_files) > 0:
        first_vid = video_files[0]
        print(f"只分析第一組影片: {first_vid}")
        
        df_vid = df_all[df_all['meta_file'] == first_vid].sort_values('time_sec')
        
        # 產出駐波包絡圖
        plot_eel_wave_static(df_vid, first_vid)
    else:
        print("Excel 中沒有影片數據。")

if __name__ == "__main__":
    main()