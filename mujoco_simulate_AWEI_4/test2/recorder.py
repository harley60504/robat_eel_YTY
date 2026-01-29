# recorder.py
import os
import time
import queue
import threading
import cv2


class AsyncVideoRecorder:
    """
    非同步錄影器：
    - 主迴圈只負責 recorder.push(frame)
    - 背景 thread 寫檔，不會卡 sim
    - 可以 start/stop 多次
    """

    def __init__(self, out_dir="videos", fps=30, fourcc="mp4v", queue_size=500):
        self.out_dir = out_dir
        self.fps = int(fps)
        self.fourcc = fourcc
        self.queue_size = int(queue_size)

        self._q = None
        self._th = None
        self._writer = None
        self._running = False

        self._width = None
        self._height = None
        self._path = None

        os.makedirs(self.out_dir, exist_ok=True)

    def is_recording(self) -> bool:
        return bool(self._running)

    def start(self, width: int, height: int, filename: str = None):
        if self._running:
            return

        self._width = int(width)
        self._height = int(height)

        if filename is None:
            ts = time.strftime("%Y%m%d_%H%M%S")
            filename = f"rec_{ts}.mp4"

        self._path = os.path.join(self.out_dir, filename)

        self._q = queue.Queue(maxsize=self.queue_size)
        self._running = True

        fourcc = cv2.VideoWriter_fourcc(*self.fourcc)
        self._writer = cv2.VideoWriter(self._path, fourcc, self.fps, (self._width, self._height))

        def _worker():
            try:
                while True:
                    item = self._q.get()
                    if item is None:
                        break
                    self._writer.write(item)
            finally:
                try:
                    self._writer.release()
                except:
                    pass

        self._th = threading.Thread(target=_worker, daemon=True)
        self._th.start()
        print(f"[Recorder] START -> {self._path}")

    def stop(self):
        if not self._running:
            return
        self._running = False

        # ✅ 非阻塞送入 None；queue 滿就丟掉一些 frame 再塞 None
        try:
            if self._q is not None:
                try:
                    self._q.put_nowait(None)
                except queue.Full:
                    for _ in range(50):
                        try:
                            self._q.get_nowait()
                        except queue.Empty:
                            break
                    try:
                        self._q.put_nowait(None)
                    except:
                        pass
        except:
            pass

        # ✅ join 也不要卡死
        try:
            if self._th is not None:
                self._th.join(timeout=2.0)
        except:
            pass

        self._writer = None
        self._q = None
        self._th = None
        print("[Recorder] STOP")

    def push(self, frame_bgr):
        if not self._running or self._q is None:
            return
        try:
            self._q.put_nowait(frame_bgr)
        except queue.Full:
            pass