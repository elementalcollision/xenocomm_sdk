import unittest
import numpy as np
from xenocomm import (
    ConnectionManager,
    CapabilitySignaler,
    NegotiationProtocol,
    DataTranscoder,
    TransmissionManager,
    FeedbackLoop,
    DataFormat,
    NegotiationState,
    TransmissionStatus,
    FeedbackType
)

class TestXenoComm(unittest.TestCase):
    def setUp(self):
        self.conn = ConnectionManager()
        self.cap_signaler = CapabilitySignaler()
        self.negotiator = NegotiationProtocol()
        self.transcoder = DataTranscoder()
        self.transmitter = TransmissionManager()
        self.feedback = FeedbackLoop()

    def test_connection_manager(self):
        # Test context manager
        with ConnectionManager() as conn:
            conn.connect("test://localhost:8080")
            self.assertTrue(conn.is_connected())
        self.assertFalse(conn.is_connected())  # Should be disconnected after context exit

        # Test manual connection
        self.conn.connect("test://localhost:8080")
        self.assertTrue(self.conn.is_connected())
        info = self.conn.get_connection_info()
        self.assertEqual(info["host"], "localhost")
        self.assertEqual(info["port"], 8080)
        self.conn.disconnect()
        self.assertFalse(self.conn.is_connected())

    def test_capability_signaler(self):
        # Test capability registration
        self.cap_signaler.register_capability("test_capability", {"version": "1.0"})
        self.assertTrue(self.cap_signaler.has_capability("test_capability"))
        
        # Test capability listing
        capabilities = self.cap_signaler.get_capabilities()
        self.assertIn("test_capability", capabilities)
        
        # Test capability unregistration
        self.cap_signaler.unregister_capability("test_capability")
        self.assertFalse(self.cap_signaler.has_capability("test_capability"))

    def test_negotiation_protocol(self):
        # Test negotiation states
        self.assertEqual(self.negotiator.get_state(), NegotiationState.IDLE)
        
        # Test proposal
        proposal = {"protocol": "test", "version": "1.0"}
        self.negotiator.propose(proposal)
        self.assertEqual(self.negotiator.get_state(), NegotiationState.PROPOSING)
        
        # Test callbacks
        proposal_received = False
        def on_proposal(p):
            nonlocal proposal_received
            proposal_received = True
            self.assertEqual(p, proposal)
        
        self.negotiator.set_on_proposal_callback(on_proposal)
        self.negotiator.propose(proposal)
        self.assertTrue(proposal_received)

    def test_data_transcoder(self):
        # Test numpy array encoding/decoding
        data = np.array([1.0, 2.0, 3.0], dtype=np.float32)
        encoded = self.transcoder.encode(data, DataFormat.VECTOR_FLOAT32)
        decoded = self.transcoder.decode(encoded, DataFormat.VECTOR_FLOAT32)
        np.testing.assert_array_almost_equal(data, decoded)

        # Test int8 array
        data = np.array([1, 2, 3], dtype=np.int8)
        encoded = self.transcoder.encode(data, DataFormat.VECTOR_INT8)
        decoded = self.transcoder.decode(encoded, DataFormat.VECTOR_INT8)
        np.testing.assert_array_equal(data, decoded)

    def test_transmission_manager(self):
        # Test transmission status
        self.assertEqual(self.transmitter.get_status(), TransmissionStatus.IDLE)
        
        # Test progress callback
        progress_value = 0.0
        def on_progress(progress):
            nonlocal progress_value
            progress_value = progress
        
        self.transmitter.set_on_progress_callback(on_progress)
        data = b"test data"
        self.transmitter.send(data)
        self.assertGreater(progress_value, 0.0)

    def test_feedback_loop(self):
        # Test feedback types
        self.feedback.enable_feedback_type(FeedbackType.LATENCY)
        self.assertTrue(self.feedback.is_feedback_type_enabled(FeedbackType.LATENCY))
        
        # Test metrics
        metrics = {"latency_ms": 100.0, "throughput_mbps": 10.0, "error_rate": 0.01}
        self.feedback.report_feedback(metrics)
        current_metrics = self.feedback.get_metrics()
        self.assertEqual(current_metrics.latency_ms, 100.0)
        
        # Test callbacks
        threshold_exceeded = False
        def on_threshold_exceeded(type, value, threshold):
            nonlocal threshold_exceeded
            threshold_exceeded = True
        
        self.feedback.set_on_threshold_exceeded_callback(on_threshold_exceeded)
        high_latency = {"latency_ms": 1000.0}  # Should trigger threshold
        self.feedback.report_feedback(high_latency)
        self.assertTrue(threshold_exceeded)

if __name__ == '__main__':
    unittest.main() 