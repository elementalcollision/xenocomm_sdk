#!/usr/bin/env python3
"""
Basic example demonstrating the usage of XenoComm SDK Python bindings.
This example shows how to:
1. Set up a connection between two agents
2. Discover and signal capabilities
3. Negotiate a protocol
4. Encode and transmit data
5. Monitor feedback and metrics
"""

import numpy as np
from xenocomm import (
    ConnectionManager,
    CapabilitySignaler,
    NegotiationProtocol,
    DataTranscoder,
    TransmissionManager,
    FeedbackLoop,
    DataFormat,
    FeedbackType
)

def on_progress(progress):
    """Callback for transmission progress updates."""
    print(f"Transmission progress: {progress * 100:.1f}%")

def on_metric_update(metrics):
    """Callback for feedback metric updates."""
    print(f"Metrics update:")
    print(f"  Latency: {metrics.latency_ms:.1f}ms")
    print(f"  Throughput: {metrics.throughput_mbps:.1f}Mbps")
    print(f"  Error rate: {metrics.error_rate:.3f}")

def main():
    # Create instances of all components
    with ConnectionManager() as conn:
        cap_signaler = CapabilitySignaler()
        negotiator = NegotiationProtocol()
        transcoder = DataTranscoder()
        transmitter = TransmissionManager()
        feedback = FeedbackLoop()

        # 1. Establish connection
        print("Connecting to remote agent...")
        conn.connect("test://localhost:8080")
        print(f"Connected: {conn.get_connection_info()}")

        # 2. Register and signal capabilities
        print("\nRegistering capabilities...")
        cap_signaler.register_capability("numpy_array_processing", {
            "version": "1.0",
            "formats": ["float32", "int8"],
            "max_dimensions": 3
        })
        cap_signaler.register_capability("compression", {
            "version": "1.0",
            "algorithms": ["zlib", "lz4"],
            "levels": range(1, 10)
        })
        print("Registered capabilities:", cap_signaler.get_capabilities())

        # 3. Negotiate protocol
        print("\nNegotiating protocol...")
        proposal = {
            "protocol": "binary",
            "version": "1.0",
            "compression": "zlib",
            "compression_level": 6
        }
        negotiator.propose(proposal)
        print(f"Protocol negotiation state: {negotiator.get_state()}")

        # 4. Prepare and transmit data
        print("\nPreparing data for transmission...")
        # Create a sample numpy array
        data = np.array([
            [1.0, 2.0, 3.0],
            [4.0, 5.0, 6.0],
            [7.0, 8.0, 9.0]
        ], dtype=np.float32)
        print("Original data:\n", data)

        # Encode the data
        print("\nEncoding data...")
        encoded_data = transcoder.encode(data, DataFormat.VECTOR_FLOAT32)
        print(f"Encoded data size: {len(encoded_data)} bytes")

        # Set up transmission monitoring
        transmitter.set_on_progress_callback(on_progress)
        print("\nTransmitting data...")
        transmitter.send(encoded_data)

        # 5. Monitor feedback and metrics
        print("\nSetting up feedback monitoring...")
        feedback.enable_feedback_type(FeedbackType.LATENCY)
        feedback.enable_feedback_type(FeedbackType.THROUGHPUT)
        feedback.enable_feedback_type(FeedbackType.ERROR_RATE)
        feedback.set_on_metric_update_callback(on_metric_update)

        # Report some sample feedback
        feedback.report_feedback({
            "latency_ms": 50.0,
            "throughput_mbps": 100.0,
            "error_rate": 0.001
        })

        # Get average metrics
        print("\nAverage metrics:")
        avg_metrics = feedback.get_average_metrics()
        print(f"  Average latency: {avg_metrics.latency_ms:.1f}ms")
        print(f"  Average throughput: {avg_metrics.throughput_mbps:.1f}Mbps")
        print(f"  Average error rate: {avg_metrics.error_rate:.3f}")

if __name__ == "__main__":
    main() 