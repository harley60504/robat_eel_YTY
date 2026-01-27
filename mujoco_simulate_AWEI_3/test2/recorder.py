import os
import time
import queue
import threading
import cv2


class AsyncVideoRecorder:
    """
    非同步錄影器：
    - 主迴圈只負責 recorder.push(frame)
    - 背景 thread 寫檔，不會卡你的 sim
    - 可以 start/stop 多次
    """

    def __init__(self, out_dir="videos", fps=30, fourcc="mp4v"):
        self.out_dir = out_dir
        self.fps = int(fps)
        self.fourcc = fourcc

        self._q = None
        self._th = None
        self._writer = None
        self._running = False

        self._width = None
        self._height = None
        self._path = None

        os.makedirs(self.out_dir, exist_ok=True)

    def is_recording(self) -> bool:
        return self._running

    def start(self, width: int, height: int, filename: str = None):
        if self._running:
            return

        self._width = int(width)
        self._height = int(height)

        if filename is None:
            ts = time.strftime("%Y%m%d_%H%M%S")
            filename = f"rec_{ts}.mp4"

        self._path = os.path.join(self.out_dir, filename)

        self._q = queue.Queue(maxsize=500)  # 防爆記憶體
        self._running = True

        fourcc = cv2.VideoWriter_fourcc(*self.fourcc)
        self._writer = cv2.VideoWriter(self._path, fourcc, self.fps, (self._width, self._height))

        def _worker():
            while True:
                item = self._q.get()
                if item is None:
                    break
                self._writer.write(item)
            self._writer.release()

        self._th = threading.Thread(target=_worker, daemon=True)
        self._th.start()
        print(f"[Recorder] START -> {self._path}")

    def stop(self):
        if not self._running:
            return
        self._running = False
        try:
            self._q.put(None)
        except:
            pass
        print("[Recorder] STOP")

    def push(self, frame_bgr):
        """
        frame_bgr: OpenCV BGR frame
        """
        if not self._running:
            return
        try:
            self._q.put_nowait(frame_bgr)
        except queue.Full:
            # 太慢就丟 frame，避免卡 sim
            pass
