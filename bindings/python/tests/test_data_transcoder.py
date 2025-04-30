import pytest
import numpy as np
from xenocomm import DataTranscoder, DataFormat, TranscodingError, TranscodingMetadata

def test_transcoding_metadata():
    # Test default initialization
    meta = TranscodingMetadata()
    assert meta.format == DataFormat.VECTOR_FLOAT32
    assert meta.scale_factor == 1.0
    assert meta.version == 1
    assert meta.element_count == 0
    assert meta.element_size == 0
    assert isinstance(meta.dimensions, list)
    assert isinstance(meta.compression_algorithm, str)
    
    # Test string representation
    assert str(meta).startswith("TranscodingMetadata(")

def test_float32_transcoding():
    transcoder = DataTranscoder()
    
    # Create test data
    data = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float32)
    
    # Test encoding
    encoded = transcoder.encode_float32(data)
    assert isinstance(encoded, bytes) or isinstance(encoded, bytearray)
    
    # Test metadata
    meta = transcoder.get_metadata(encoded)
    assert meta.format == DataFormat.VECTOR_FLOAT32
    assert meta.element_count == 4
    assert meta.element_size == 4  # float32 = 4 bytes
    
    # Test decoding
    decoded = transcoder.decode_float32(encoded)
    assert isinstance(decoded, np.ndarray)
    assert decoded.dtype == np.float32
    assert np.array_equal(decoded, data)

def test_int8_transcoding():
    transcoder = DataTranscoder()
    
    # Create test data
    data = np.array([1, 2, 3, 4], dtype=np.int8)
    
    # Test encoding
    encoded = transcoder.encode_int8(data)
    assert isinstance(encoded, bytes) or isinstance(encoded, bytearray)
    
    # Test metadata
    meta = transcoder.get_metadata(encoded)
    assert meta.format == DataFormat.VECTOR_INT8
    assert meta.element_count == 4
    assert meta.element_size == 1  # int8 = 1 byte
    
    # Test decoding
    decoded = transcoder.decode_int8(encoded)
    assert isinstance(decoded, np.ndarray)
    assert decoded.dtype == np.int8
    assert np.array_equal(decoded, data)

def test_multidimensional_array():
    transcoder = DataTranscoder()
    
    # Create 2D test data
    data = np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float32)
    
    # Test encoding
    encoded = transcoder.encode_float32(data)
    
    # Test metadata
    meta = transcoder.get_metadata(encoded)
    assert meta.format == DataFormat.VECTOR_FLOAT32
    assert meta.element_count == 4
    assert len(meta.dimensions) == 2
    assert meta.dimensions == [2, 2]
    
    # Test decoding
    decoded = transcoder.decode_float32(encoded)
    assert isinstance(decoded, np.ndarray)
    assert decoded.shape == (2, 2)
    assert np.array_equal(decoded, data)

def test_raw_data_transcoding():
    transcoder = DataTranscoder()
    
    # Test raw bytes encoding/decoding
    data = b"Hello, World!"
    encoded = transcoder.encode(data, DataFormat.BINARY_CUSTOM)
    
    # Test metadata
    meta = transcoder.get_metadata(encoded)
    assert meta.format == DataFormat.BINARY_CUSTOM
    assert meta.element_count == len(data)
    
    # Test decoding
    decoded = transcoder.decode(encoded, DataFormat.BINARY_CUSTOM)
    assert decoded == data

def test_format_validation():
    transcoder = DataTranscoder()
    
    # Test valid float32 data
    data = np.array([1.0, 2.0], dtype=np.float32)
    assert transcoder.is_valid_format(data, DataFormat.VECTOR_FLOAT32)
    
    # Test invalid format
    with pytest.raises(TranscodingError):
        transcoder.encode_float32(np.array([1, 2], dtype=np.int32))

def test_error_handling():
    transcoder = DataTranscoder()
    
    # Test encoding with invalid data
    with pytest.raises(TranscodingError):
        transcoder.encode(None, DataFormat.BINARY_CUSTOM)
    
    # Test decoding with invalid format
    with pytest.raises(TranscodingError):
        transcoder.decode_float32(b"invalid data") 