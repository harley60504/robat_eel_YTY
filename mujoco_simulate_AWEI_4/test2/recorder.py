# recorder.py
import os
import cv2
import queue
import threading
import time

class AsyncVideoRecorder:
    def __init__(self, out_dir="videos", fps=30, max_queue=64):
        self.out_dir = out_dir
        self.fps = fps
        self.max_queue = max_queue

        self.q = queue.Queue(maxsize=max_queue)
        self.writer = None
        self.thread = None
        self.running = False

        os.makedirs(out_dir, exist_ok=True)

    def start(self, width, height, filename):
        self.stop()

        path = os.path.join(self.out_dir, filename)
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        self.writer = cv2.VideoWriter(path, fourcc, self.fps, (width, height))

        self.running = True
        self.thread = threading.Thread(target=self._worker, daemon=True)
        self.thread.start()
        print("[Recorder] START ->", path)

    def _worker(self):
        while self.running or not self.q.empty():
            try:
                frame = self.q.get(timeout=0.1)
                self.writer.write(frame)
            except queue.Empty:
                continue

    def push(self, frame_bgr):
        if not self.running:
            return
        try:
            # ✅ 關鍵：滿了就丟，不阻塞
            self.q.put_nowait(frame_bgr)
        except queue.Full:
            pass

    def is_recording(self):
        return self.running

    def stop(self):
        if not self.running:
            return
        self.running = False
        if self.thread is not None:
            self.thread.join(timeout=1.0)
        if self.writer is not None:
            self.writer.release()
        self.writer = None
        self.thread = None
        with self.q.mutex:
            self.q.queue.clear()
        print("[Recorder] STOP")
