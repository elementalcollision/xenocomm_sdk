import pytest
from unittest.mock import patch, MagicMock
from xenocomm.graphql_adapter import GraphQLAdapter
from xenocomm.rate_limiter import RateLimiter
from xenocomm.cache_manager import CacheManager

@patch("requests.post")
def test_rate_limiting_blocks(mock_post):
    rl = RateLimiter(capacity=1, refill_rate=0)
    adapter = GraphQLAdapter(rate_limiter=rl)
    adapter.configure("http://test")
    # First request should pass
    mock_post.return_value = MagicMock(status_code=200, json=lambda: {"data": {"ok": True}}, raise_for_status=lambda: None)
    adapter.query("query { ok }", variables={})
    # Second request should block (simulate by timeout)
    import threading
    result = []
    def call():
        result.append(adapter.query("query { ok }", variables={}))
    t = threading.Thread(target=call)
    t.start()
    t.join(timeout=0.2)
    assert t.is_alive()  # Should still be waiting for token
    # Clean up
    rl.tokens = 1  # Refill manually
    t.join(timeout=0.2)

@patch("requests.post")
def test_caching_returns_cached(mock_post):
    cache = CacheManager()
    adapter = GraphQLAdapter(cache_manager=cache)
    adapter.configure("http://test")
    mock_post.return_value = MagicMock(status_code=200, json=lambda: {"data": {"foo": 1}}, raise_for_status=lambda: None)
    # First call populates cache
    result1 = adapter.query("query { foo }", variables={})
    # Change mock to ensure cache is used
    mock_post.return_value = MagicMock(status_code=200, json=lambda: {"data": {"foo": 2}}, raise_for_status=lambda: None)
    result2 = adapter.query("query { foo }", variables={})
    assert result1 == result2 == {"foo": 1}
    # Invalidate cache and try again
    cache.invalidate_all()
    result3 = adapter.query("query { foo }", variables={})
    assert result3 == {"foo": 2} 