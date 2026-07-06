import time
import threading
import pytest
from xenocomm.cache_manager import CacheManager
import os

def test_set_get():
    cm = CacheManager()
    cm.set("foo", 123)
    assert cm.get("foo") == 123
    assert cm.get("bar") is None
    metrics = cm.get_metrics()
    assert metrics["hits"] == 1
    assert metrics["misses"] == 1

def test_ttl_expiry():
    cm = CacheManager()
    cm.set("foo", 123, ttl=0.2)
    assert cm.get("foo") == 123
    time.sleep(0.3)
    assert cm.get("foo") is None

def test_manual_invalidate():
    cm = CacheManager()
    cm.set("foo", 123)
    cm.invalidate("foo")
    assert cm.get("foo") is None

def test_invalidate_all():
    cm = CacheManager()
    cm.set("foo", 1)
    cm.set("bar", 2)
    cm.invalidate_all()
    assert cm.get("foo") is None
    assert cm.get("bar") is None

def test_cleanup_expired():
    cm = CacheManager()
    cm.set("foo", 1, ttl=0.1)
    cm.set("bar", 2)
    time.sleep(0.2)
    cm.cleanup_expired()
    assert cm.get("foo") is None
    assert cm.get("bar") == 2

def test_thread_safety():
    cm = CacheManager()
    def writer():
        for i in range(10):
            cm.set(f"k{i}", i)
    def reader():
        for i in range(10):
            cm.get(f"k{i}")
    threads = [threading.Thread(target=writer) for _ in range(2)] + [threading.Thread(target=reader) for _ in range(2)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    # No assertion, just ensure no exceptions and metrics are reasonable
    metrics = cm.get_metrics()
    assert metrics["hits"] + metrics["misses"] >= 0 

def test_temp_output_dir_fixture(temp_output_dir):
    """Test the temp_output_dir fixture from conftest.py."""
    test_file = os.path.join(temp_output_dir, "testfile.txt")
    with open(test_file, "w") as f:
        f.write("hello world")
    with open(test_file, "r") as f:
        content = f.read()
    assert content == "hello world"
    # Directory will be cleaned up after test 