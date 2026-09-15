"""Asynchronous status-message dispatch shared by kiwicgen frontends."""

from __future__ import annotations

from collections.abc import Callable
import queue
import threading


_STOP = object()


class AsyncLogDispatcher:
    """Serialize frontend log output through one dedicated listener thread."""

    def __init__(self, sink: Callable[[str], None]) -> None:
        self._sink = sink
        self._queue: queue.Queue[str | object] = queue.Queue()
        self._closed = False
        self._thread = threading.Thread(
            target=self._run,
            name="kiwicgen-log",
            daemon=True,
        )
        self._thread.start()

    def log(self, message: str) -> None:
        """Queue one already-structured status message for frontend output."""
        if self._closed:
            return
        self._queue.put(str(message))

    def flush(self) -> None:
        """Wait until every message queued so far has reached the sink."""
        self._queue.join()

    def close(self) -> None:
        """Flush pending messages and stop the listener thread."""
        if self._closed:
            return
        self.flush()
        self._closed = True
        self._queue.put(_STOP)
        self._thread.join()

    def _run(self) -> None:
        """Forward queued messages to the configured sink in FIFO order."""
        while True:
            item = self._queue.get()
            try:
                if item is _STOP:
                    return

                try:
                    self._sink(str(item))
                except Exception:
                    pass
            finally:
                self._queue.task_done()

    def __enter__(self) -> AsyncLogDispatcher:
        return self

    def __exit__(self, _exc_type: object, _exc_value: object, _traceback: object) -> None:
        self.close()
