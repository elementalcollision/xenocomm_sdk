import time
import threading
import pytest
from xenocomm.rate_limiter import RateLimiter

def test_acquire_and_deny():
    rl = RateLimiter(capacity=2, refill_rate=1)
    assert rl.acquire() is True
    assert rl.acquire() is True
    # Should deny now (no tokens left)
    assert rl.acquire() is False
    metrics = rl.get_metrics()
    assert metrics["acquired"] == 2
    assert metrics["denied"] == 1

def test_refill():
    rl = RateLimiter(capacity=2, refill_rate=2)  # 2 tokens/sec
    assert rl.acquire() is True
    assert rl.acquire() is True
    assert rl.acquire() is False
    time.sleep(0.6)  # Should refill ~1.2 tokens
    assert rl.acquire() is True  # Should succeed after refill

def test_thread_safety():
    rl = RateLimiter(capacity=5, refill_rate=5)
    results = []
    def worker():
        for _ in range(3):
            results.append(rl.acquire())
    threads = [threading.Thread(target=worker) for _ in range(3)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    # At most 5 tokens can be acquired at start, rest will be denied
    assert results.count(True) == 5
    assert results.count(False) == 4

def test_metrics():
    rl = RateLimiter(capacity=1, refill_rate=0)
    rl.acquire()
    rl.acquire()
    metrics = rl.get_metrics()
    assert metrics["acquired"] == 1
    assert metrics["denied"] == 1 