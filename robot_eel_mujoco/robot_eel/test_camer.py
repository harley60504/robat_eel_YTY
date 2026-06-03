# import cv2
# import os
# import time

# # 設定資料夾
# SAVE_FOLDER = "recordings"
# if not os.path.exists(SAVE_FOLDER):
#     os.makedirs(SAVE_FOLDER)

# # RTSP 串流網址
# rtsp_url = "rtsp://admin:184342@192.168.0.102:554/live/profile.0/video"
# cap = cv2.VideoCapture(rtsp_url, cv2.CAP_FFMPEG)

# if not cap.isOpened():
#     print("❌ 串流開啟失敗")
#     exit()

# # --- 設定標準 1080p 規格 ---
# TARGET_W = 1920
# TARGET_H = 1080
# fps = 20.0 

# out = None
# is_recording = False

# # 視窗設定：WINDOW_NORMAL 允許縮放視窗大小，但內部比例會維持我們設定的 1080p
# cv2.namedWindow("CCTV_1080p", cv2.WINDOW_NORMAL)
# # 初始顯示窗設小一點 (例如螢幕的一半)，避免擋住程式碼
# cv2.resizeWindow("CCTV_1080p", 960, 540)

# print(f"--- 系統啟動 (標準 1080p 模式) ---")
# print("按 'r' 鍵：開始 / 停止錄影")
# print("按 'q' 鍵：結束程式")

# while True:
#     ok, frame = cap.read()
#     if not ok or frame is None:
#         break

#     # 1. 強制將原始畫面縮放至 1920x1080
#     # 這是解決「範圍看起來不一樣」的最關鍵步驟
#     display_frame = cv2.resize(frame, (TARGET_W, TARGET_H), interpolation=cv2.INTER_LANCZOS4)

#     # 2. 建立錄影用的副本
#     # 我們在畫面上加上 REC 字樣和紅點，這也會被錄進去
#     record_frame = display_frame.copy()
#     if is_recording:
#         cv2.circle(record_frame, (50, 50), 20, (0, 0, 255), -1)
#         cv2.putText(record_frame, "REC NOW", (85, 65), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 0, 255), 3)

#     # 3. 顯示 (所見)
#     cv2.imshow("CCTV_1080p", record_frame)

#     # 4. 錄製 (即所得)
#     if is_recording and out is not None:
#         out.write(record_frame)

#     key = cv2.waitKey(1) & 0xFF
    
#     if key == ord('r'):
#         if not is_recording:
#             timestamp = time.strftime("%Y%m%d_%H%M%S")
#             filename = os.path.join(SAVE_FOLDER, f"video_{timestamp}.mp4")
            
#             # 使用 avc1 (H.264) 編碼
#             fourcc = cv2.VideoWriter_fourcc(*'avc1')
#             out = cv2.VideoWriter(filename, fourcc, fps, (TARGET_W, TARGET_H))
            
#             # 如果 avc1 在你的電腦不支援，改用 mp4v
#             if not out.isOpened():
#                 fourcc = cv2.VideoWriter_fourcc(*'mp4v')
#                 out = cv2.VideoWriter(filename, fourcc, fps, (TARGET_W, TARGET_H))

#             is_recording = True
#             print(f"🔴 開始錄影 (1080p): {filename}")
#         else:
#             is_recording = False
#             if out is not None:
#                 out.release()
#             print("⏹️ 錄影停止並存檔。")

#     elif key == ord('q'):
#         break

# cap.release()
# if out is not None:
#     out.release()
# cv2.destroyAllWindows()
# print("系統已關閉。")
import cv2
import os
import time

# 設定資料夾
SAVE_FOLDER = "recordings"
if not os.path.exists(SAVE_FOLDER):
    os.makedirs(SAVE_FOLDER)

# RTSP 串流網址
rtsp_url = "rtsp://admin:184342@192.168.0.102:554/live/profile.0/video"
cap = cv2.VideoCapture(rtsp_url, cv2.CAP_FFMPEG)

if not cap.isOpened():
    print("❌ 串流開啟失敗")
    exit()

# --- 設定規格 ---
TARGET_W = 1920
TARGET_H = 1080
fps = 20.0 

# 轉 90 度後的規格
RECORD_W = TARGET_H
RECORD_H = TARGET_W

out = None
is_recording = False

cv2.namedWindow("CCTV_Clean_Record", cv2.WINDOW_NORMAL)
cv2.resizeWindow("CCTV_Clean_Record", 540, 960)

print(f"--- 系統啟動 (旋轉 90 度 + 乾淨錄影模式) ---")
print("按 'r' 鍵：開始 / 停止錄影")
print("按 'q' 鍵：結束程式")

while True:
    ok, frame = cap.read()
    if not ok or frame is None:
        break

    # 1. 影像處理：縮放並旋轉 90 度
    temp_frame = cv2.resize(frame, (TARGET_W, TARGET_H), interpolation=cv2.INTER_LANCZOS4)
    # 這是我們要的「最終畫面」
    clean_frame = cv2.rotate(temp_frame, cv2.ROTATE_90_CLOCKWISE)

    # 2. 💡 錄製畫面 (在畫字之前就寫入，保證影片乾淨)
    if is_recording and out is not None:
        out.write(clean_frame)

    # 3. 建立顯示用的副本，並在上面畫字
    display_preview = clean_frame.copy()
    if is_recording:
        # 只在預覽視窗畫上 REC，不會影響錄影檔
        cv2.circle(display_preview, (50, 50), 20, (0, 0, 255), -1)
        cv2.putText(display_preview, "REC NOW", (85, 65), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 0, 255), 3)

    # 4. 顯示預覽 (有字)
    cv2.imshow("CCTV_Clean_Record", display_preview)

    key = cv2.waitKey(1) & 0xFF
    
    if key == ord('r'):
        if not is_recording:
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            filename = os.path.join(SAVE_FOLDER, f"clean_v_{timestamp}.mp4")
            
            fourcc = cv2.VideoWriter_fourcc(*'avc1')
            out = cv2.VideoWriter(filename, fourcc, fps, (RECORD_W, RECORD_H))
            
            if not out.isOpened():
                fourcc = cv2.VideoWriter_fourcc(*'mp4v')
                out = cv2.VideoWriter(filename, fourcc, fps, (RECORD_W, RECORD_H))

            is_recording = True
            print(f"🔴 開始錄影 (乾淨畫面): {filename}")
        else:
            is_recording = False
            if out is not None:
                out.release()
            print("⏹️ 錄影停止並存檔。")

    elif key == ord('q'):
        break

cap.release()
if out is not None:
    out.release()
cv2.destroyAllWindows()
print("系統已關閉。")