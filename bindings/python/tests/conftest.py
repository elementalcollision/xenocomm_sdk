"""
conftest.py

Shared pytest fixtures and test helpers for XenoComm SDK Python bindings.
Add fixtures here to be used across multiple test modules.
"""

import pytest
from unittest.mock import MagicMock
import tempfile
import shutil

@pytest.fixture
def mock_connection_manager():
    """Fixture that provides a mock ConnectionManager instance."""
    from xenocomm import ConnectionManager
    mock = MagicMock(spec=ConnectionManager)
    # Set up default mock behavior if needed
    mock.is_connected.return_value = True
    return mock

@pytest.fixture
def temp_output_dir():
    """Fixture that provides a temporary directory for test output and cleans up after use."""
    dirpath = tempfile.mkdtemp()
    yield dirpath
    shutil.rmtree(dirpath)

# Example placeholder fixture (uncomment and implement as needed)
# import pytest
#
# @pytest.fixture
# def example_fixture():
#     yield 