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

### Shared Test Fixtures

The test suite provides shared pytest fixtures in `tests/conftest.py` to simplify and standardize test setup across modules:

- **mock_connection_manager**: Provides a mock `ConnectionManager` instance using `unittest.mock.MagicMock`. Use this fixture in your tests to stub out connection logic or simulate connection states.

  Example usage:
  ```python
  def test_with_mock_manager(mock_connection_manager):
      assert mock_connection_manager.is_connected() is True
      mock_connection_manager.establish.return_value = "mock_conn"
      assert mock_connection_manager.establish("test") == "mock_conn"
  ```

- **temp_output_dir**: Provides a temporary directory for test output, automatically cleaned up after the test completes. Useful for tests that need to write/read files without polluting the workspace.

  Example usage:
  ```python
  def test_with_temp_dir(temp_output_dir):
      test_file = os.path.join(temp_output_dir, "testfile.txt")
      with open(test_file, "w") as f:
          f.write("hello world")
      with open(test_file, "r") as f:
          content = f.read()
      assert content == "hello world"
  ```

To use these fixtures, simply add them as arguments to your test functions. Pytest will automatically provide the fixture when running the tests.

## License

This project is licensed under the same terms as the main XenoComm SDK. 