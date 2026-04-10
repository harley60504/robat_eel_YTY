import cv2
import numpy as np
import math

rtsp_url = "rtsp://admin:184342@192.168.0.102:554/live/profile.0/video"
cap = cv2.VideoCapture(rtsp_url, cv2.CAP_FFMPEG)

if not cap.isOpened():
    print("❌ 串流開啟失敗")
    exit()

tracked = []   # [x, y, r, life]


def update_tracked(tracked, new_dots):
    MAX_LIFE = 8
    DIST = 60

    for t in tracked:
        t[3] -= 1

    used = set()

    # 舊點找新點（避免合併）
    for t in tracked:
        best_i = -1
        best_d = 9999

        for i, (cx, cy, r) in enumerate(new_dots):
            if i in used:
                continue
            d = ((cx - t[0])**2 + (cy - t[1])**2) ** 0.5
            if d < best_d:
                best_d = d
                best_i = i

        if best_d < DIST and best_i != -1:
            cx, cy, r = new_dots[best_i]

            # 幀間平滑（抗跳）
            t[0] = int(t[0]*0.7 + cx*0.3)
            t[1] = int(t[1]*0.7 + cy*0.3)
            t[2] = int(t[2]*0.7 + r*0.3)
            t[3] = MAX_LIFE

            used.add(best_i)

    # 新點補進
    for i, (cx, cy, r) in enumerate(new_dots):
        if i not in used:
            tracked.append([cx, cy, r, MAX_LIFE])

    tracked[:] = [t for t in tracked if t[3] > 0]
    return tracked


def detect_red_dots(frame):
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    # ===== 紅色甜蜜區 =====
    lower_red1 = np.array([0, 85, 60])
    upper_red1 = np.array([20, 255, 255])

    lower_red2 = np.array([160, 85, 60])
    upper_red2 = np.array([180, 255, 255])

    mask1 = cv2.inRange(hsv, lower_red1, upper_red1)
    mask2 = cv2.inRange(hsv, lower_red2, upper_red2)
    mask = cv2.bitwise_or(mask1, mask2)

    # ===== 去雜訊 + 補洞 =====
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    dots = []

    for cnt in contours:
        area = cv2.contourArea(cnt)
        if area < 5 or area > 1500:
            continue

        peri = cv2.arcLength(cnt, True)
        if peri == 0:
            continue

        circularity = 4 * math.pi * area / (peri * peri)
        if circularity < 0.38:
            continue

        x, y, w, h = cv2.boundingRect(cnt)
        aspect = w / float(h)
        if aspect < 0.5 or aspect > 1.5:
            continue

        hull = cv2.convexHull(cnt)
        hull_area = cv2.contourArea(hull)
        if hull_area == 0:
            continue
        solidity = area / hull_area
        if solidity < 0.68:
            continue

        (cx, cy), r = cv2.minEnclosingCircle(cnt)
        if r < 2 or r > 50:
            continue

        fill_ratio = area / (math.pi * r * r)
        if fill_ratio < 0.15:
            continue

        # ===== 紅色純度驗證 =====
        roi = frame[y:y+h, x:x+w]
        if roi.size == 0:
            continue

        mean_b = np.mean(roi[:, :, 0])
        mean_g = np.mean(roi[:, :, 1])
        mean_r = np.mean(roi[:, :, 2])

        # 紅必須明顯大於藍綠
        if mean_r < mean_g + 25:
            continue
        if mean_r < mean_b + 25:
            continue

        # 亮度門檻（放鬆）
        if mean_r < 85:
            continue

        dots.append((int(cx), int(cy), int(r)))

    return dots, mask


while True:
    ok, frame = cap.read()
    if not ok or frame is None:
        print("❌ 讀取影像失敗")
        break

    dots, mask = detect_red_dots(frame)
    tracked = update_tracked(tracked, dots)

    for x, y, r, life in tracked:
        cv2.circle(frame, (x, y), r, (0, 255, 0), 2)
        cv2.putText(frame, f"RED {life}",
                    (x - 20, y - 10),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.5, (0, 255, 0), 1)

    cv2.imshow("Stable Red Dot Detect", frame)
    cv2.imshow("Mask", mask)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
