# recorder.py
import os
import cv2
import queue
import threading
import time

class AsyncVideoRecorder:
    def __init__(self, out_dir="videos", fps=30, max_queue=256):
        self.out_dir = out_dir
        self.fps = fps
        self.max_queue = max_queue

        self.q = queue.Queue(maxsize=max_queue)
        self.writer = None
        self.thread = None
        self.running = False

        # ✅ 避免 start/stop 與 worker 同時碰 writer
        self._wlock = threading.Lock()

        os.makedirs(out_dir, exist_ok=True)

    def start(self, width, height, filename):
        # 先停掉舊的
        self.stop()

        path = os.path.join(self.out_dir, filename)

        # mp4v：跨平台相對穩
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        writer = cv2.VideoWriter(path, fourcc, self.fps, (width, height))

        # ✅ 重要：確認 writer 成功開啟（Windows 上偶爾會失敗）
        if not writer.isOpened():
            raise RuntimeError(f"[Recorder] VideoWriter open failed: {path}")

        with self._wlock:
            self.writer = writer

        self.running = True
        self.thread = threading.Thread(target=self._worker, daemon=True)
        self.thread.start()
        print("[Recorder] START ->", path)

    def _worker(self):
        while self.running or (not self.q.empty()):
            try:
                frame = self.q.get(timeout=0.1)
            except queue.Empty:
                continue

            # ✅ writer 可能在 stop() 被釋放，這裡要保護
            with self._wlock:
                w = self.writer
            if w is None:
                continue

            try:
                w.write(frame)
            except Exception:
                # 寫入失敗就丟掉，避免卡死
                continue

    def push(self, frame_bgr):
        if not self.running:
            return
        try:
            # ✅ 滿了就丟，不阻塞（你原本的關鍵策略保留）
            self.q.put_nowait(frame_bgr)
        except queue.Full:
            pass

    def is_recording(self):
        return self.running

    def stop(self):
        if not self.running:
            return

        self.running = False

        # ✅ 給 worker 足夠時間 flush（1080p60 很需要）
        if self.thread is not None:
            self.thread.join(timeout=5.0)

        # ✅ 釋放 writer
        with self._wlock:
            if self.writer is not None:
                try:
                    self.writer.release()
                except Exception:
                    pass
            self.writer = None

        self.thread = None

        # ✅ 清空 queue（不阻塞）
        try:
            while True:
                self.q.get_nowait()
        except queue.Empty:
            pass

        print("[Recorder] STOP")
