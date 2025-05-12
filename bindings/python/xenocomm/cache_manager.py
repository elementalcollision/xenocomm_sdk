import time
import threading

class CacheManager:
    """
    Thread-safe cache manager with TTL, manual, and conditional invalidation.
    Tracks cache hits/misses for monitoring.
    Suitable for use in API adapters and other caching scenarios.
    """
    def __init__(self):
        """
        Initialize the cache manager.
        """
        self.cache = {}
        self.ttls = {}
        self.metrics = {"hits": 0, "misses": 0}
        self.lock = threading.RLock()

    def get(self, key):
        """
        Retrieve a value from the cache.

        Args:
            key (str): The cache key.

        Returns:
            Any: The cached value, or None if not found or expired.
        Increments hit/miss metrics.
        """
        with self.lock:
            if key in self.cache:
                if key in self.ttls and time.time() > self.ttls[key]:
                    # Expired
                    self._invalidate(key)
                    self.metrics["misses"] += 1
                    return None
                self.metrics["hits"] += 1
                return self.cache[key]
            self.metrics["misses"] += 1
            return None

    def set(self, key, value, ttl=None):
        """
        Store a value in the cache with optional TTL (seconds).

        Args:
            key (str): The cache key.
            value (Any): The value to cache.
            ttl (float, optional): Time-to-live in seconds. If None, no expiry.
        """
        with self.lock:
            self.cache[key] = value
            if ttl:
                self.ttls[key] = time.time() + ttl

    def invalidate(self, key):
        """
        Manually remove a key from the cache.

        Args:
            key (str): The cache key to remove.
        """
        with self.lock:
            self._invalidate(key)

    def invalidate_all(self):
        """
        Remove all entries from the cache.
        """
        with self.lock:
            self.cache.clear()
            self.ttls.clear()

    def cleanup_expired(self):
        """
        Remove all expired entries from the cache.
        """
        with self.lock:
            now = time.time()
            expired = [k for k, exp in self.ttls.items() if now > exp]
            for k in expired:
                self._invalidate(k)

    def _invalidate(self, key):
        if key in self.cache:
            del self.cache[key]
        if key in self.ttls:
            del self.ttls[key]

    def get_metrics(self):
        """
        Return current cache metrics.

        Returns:
            dict: Dictionary with 'hits' and 'misses' counts.
        """
        with self.lock:
            return dict(self.metrics) 