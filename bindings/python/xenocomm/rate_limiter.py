import time
import threading

class RateLimiter:
    """
    Thread-safe token bucket rate limiter for controlling request rates.

    Supports per-endpoint configuration by instantiating separate objects.
    Tracks acquired/denied metrics for monitoring.
    """
    def __init__(self, capacity, refill_rate):
        """
        Initialize the rate limiter.

        Args:
            capacity (int): Maximum number of tokens (requests) allowed in the bucket.
            refill_rate (float): Number of tokens added per second.
        """
        self.capacity = capacity
        self.refill_rate = refill_rate
        self.tokens = capacity
        self.last_refill = time.time()
        self.lock = threading.RLock()
        self.metrics = {"acquired": 0, "denied": 0}

    def acquire(self, tokens=1):
        """
        Attempt to acquire tokens for a request.

        Args:
            tokens (int): Number of tokens to acquire (default: 1).

        Returns:
            bool: True if tokens were acquired, False if rate limit exceeded.
        """
        with self.lock:
            self._refill()
            if self.tokens >= tokens:
                self.tokens -= tokens
                self.metrics["acquired"] += 1
                return True
            self.metrics["denied"] += 1
            return False

    def _refill(self):
        """
        Refill tokens in the bucket based on elapsed time and refill rate.
        """
        now = time.time()
        elapsed = now - self.last_refill
        new_tokens = elapsed * self.refill_rate
        self.tokens = min(self.capacity, self.tokens + new_tokens)
        self.last_refill = now

    def get_metrics(self):
        """
        Return current rate limiter metrics.

        Returns:
            dict: Dictionary with 'acquired' and 'denied' request counts.
        """
        with self.lock:
            return dict(self.metrics) 