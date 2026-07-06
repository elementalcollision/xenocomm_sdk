import tracemalloc
import gc
import unittest

# Import C++-bound classes (adjust import as needed for your module)
try:
    from xenocomm import ConnectionManager, DataTranscoder  # Example module name
except ImportError:
    from my_module import ConnectionManager, DataTranscoder  # Fallback for test scaffolding

class MemoryLeakTests(unittest.TestCase):
    def setUp(self):
        gc.collect()
        tracemalloc.start()

    def tearDown(self):
        tracemalloc.stop()

    def test_connection_manager_memory_usage(self):
        """Test repeated creation/destruction of ConnectionManager for leaks."""
        snapshot1 = tracemalloc.take_snapshot()
        for _ in range(1000):
            manager = ConnectionManager()
            manager.connect("test://endpoint", {"timeout": 100})
        gc.collect()
        snapshot2 = tracemalloc.take_snapshot()
        top_stats = snapshot2.compare_to(snapshot1, 'lineno')
        total_diff = sum(stat.size_diff for stat in top_stats)
        self.assertLess(total_diff, 10000, f"Memory grew by {total_diff} bytes (expected <10KB)")

    def test_reference_cycles(self):
        """Test for reference cycles with callbacks."""
        snapshot1 = tracemalloc.take_snapshot()
        for _ in range(100):
            manager = ConnectionManager()
            def callback(endpoint, reason):
                pass
            manager.register_disconnect_handler(callback)
        gc.collect()
        snapshot2 = tracemalloc.take_snapshot()
        top_stats = snapshot2.compare_to(snapshot1, 'lineno')
        total_diff = sum(stat.size_diff for stat in top_stats)
        self.assertLess(total_diff, 5000, f"Memory grew by {total_diff} bytes (expected <5KB)")

    def test_buffer_protocol_memory(self):
        """Test memory management with buffer protocol and large buffers."""
        if not hasattr(DataTranscoder, 'process_buffer'):
            self.skipTest("DataTranscoder.process_buffer not available")
        snapshot1 = tracemalloc.take_snapshot()
        for _ in range(500):
            transcoder = DataTranscoder()
            buffer = bytearray(1024 * 1024)  # 1MB buffer
            result = transcoder.process_buffer(buffer)
        gc.collect()
        snapshot2 = tracemalloc.take_snapshot()
        top_stats = snapshot2.compare_to(snapshot1, 'lineno')
        total_diff = sum(stat.size_diff for stat in top_stats)
        self.assertLess(total_diff, 50000, f"Memory grew by {total_diff} bytes (expected <50KB)")

if __name__ == "__main__":
    unittest.main() 