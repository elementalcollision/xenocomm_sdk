# XenoComm SDK Python Bindings

Python bindings for the XenoComm SDK, providing a Pythonic interface to the high-performance communication framework for AI agents.

## Installation

### Prerequisites

- Python 3.7 or later
- CMake 3.15 or later
- C++ compiler with C++17 support
- pybind11 2.10.0 or later

### Installing from Source

1. Clone the repository:
   ```bash
   git clone https://github.com/elementalcollision/xenocomm_sdk.git
   cd xenocomm_sdk
   ```

2. Build and install:
   ```bash
   pip install ./bindings/python
   ```

## Usage

```python
from xenocomm import ConnectionManager, CapabilitySignaler

# Create a connection manager
conn = ConnectionManager()

# Use context manager for automatic cleanup
with ConnectionManager() as conn:
    # Connect to a remote agent
    conn.connect("remote_agent_address")
    
    # Check connection status
    if conn.is_connected():
        print("Connected successfully!")
        
    # Get connection information
    info = conn.get_connection_info()
    print(f"Connected to: {info}")
```

## API Reference

### ConnectionManager

The main class for managing connections between AI agents.

- `connect(address: str) -> bool`: Connect to a remote agent
- `disconnect() -> None`: Disconnect from the current connection
- `is_connected() -> bool`: Check if currently connected
- `get_connection_info() -> dict`: Get information about the current connection

### CapabilitySignaler

Handles capability discovery and signaling between agents.

### NegotiationProtocol

Manages protocol negotiation between agents.

### DataTranscoder

Handles data encoding and decoding.

### TransmissionManager

Manages data transmission between agents.

### FeedbackLoop

Provides adaptive feedback mechanisms.

## Development

### Building from Source

1. Install dependencies:
   ```bash
   pip install pybind11
   ```

2. Build in development mode:
   ```bash
   pip install -e ./bindings/python
   ```

### Running Tests

```bash
cd bindings/python
python -m pytest tests/
```

## License

This project is licensed under the same terms as the main XenoComm SDK. 