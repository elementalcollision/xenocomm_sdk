import pytest
from unittest.mock import patch, MagicMock
from xenocomm.rest_adapter import RestApiAdapter
from xenocomm.rate_limiter import RateLimiter
from xenocomm.cache_manager import CacheManager

@patch("requests.request")
def test_rate_limiting_blocks(mock_request):
    rl = RateLimiter(capacity=1, refill_rate=0)
    adapter = RestApiAdapter(rate_limiter=rl)
    adapter.configure("http://test")
    # First request should pass
    mock_request.return_value = MagicMock(status_code=200, json=lambda: {"ok": True}, raise_for_status=lambda: None)
    adapter.execute("foo", "GET")
    # Second request should block (simulate by timeout)
    import threading
    result = []
    def call():
        result.append(adapter.execute("foo", "GET"))
    t = threading.Thread(target=call)
    t.start()
    t.join(timeout=0.2)
    assert t.is_alive()  # Should still be waiting for token
    # Clean up
    rl.tokens = 1  # Refill manually
    t.join(timeout=0.2)

@patch("requests.request")
def test_caching_returns_cached(mock_request):
    cache = CacheManager()
    adapter = RestApiAdapter(cache_manager=cache)
    adapter.configure("http://test")
    mock_request.return_value = MagicMock(status_code=200, json=lambda: {"foo": 1}, raise_for_status=lambda: None)
    # First call populates cache
    result1 = adapter.execute("foo", "GET")
    # Change mock to ensure cache is used
    mock_request.return_value = MagicMock(status_code=200, json=lambda: {"foo": 2}, raise_for_status=lambda: None)
    result2 = adapter.execute("foo", "GET")
    assert result1 == result2 == {"foo": 1}
    # Invalidate cache and try again
    cache.invalidate_all()
    result3 = adapter.execute("foo", "GET")
    assert result3 == {"foo": 2} 