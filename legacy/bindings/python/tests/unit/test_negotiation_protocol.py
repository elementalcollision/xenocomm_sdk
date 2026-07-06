import pytest
import numpy as np
from xenocomm import (
    NegotiationProtocol, NegotiableParams, ParameterPreference,
    DataFormat, CompressionAlgorithm, ErrorCorrectionScheme,
    EncryptionAlgorithm, KeyExchangeMethod, AuthenticationMethod, KeySize,
    RankedDataFormat, RankedCompression
)

def test_negotiable_params():
    # Test default initialization
    params = NegotiableParams()
    assert params.protocol_version == "1.0.0"
    assert params.security_version == "1.0.0"
    assert params.data_format == DataFormat.BINARY_CUSTOM
    assert params.compression_algorithm == CompressionAlgorithm.NONE
    assert params.error_correction == ErrorCorrectionScheme.NONE
    assert params.encryption_algorithm == EncryptionAlgorithm.NONE
    assert params.key_exchange_method == KeyExchangeMethod.NONE
    assert params.authentication_method == AuthenticationMethod.NONE
    assert params.key_size == KeySize.BITS_256
    assert isinstance(params.custom_parameters, dict)
    
    # Test string representation
    assert str(params).startswith("NegotiableParams(")
    
    # Test equality operators
    params2 = NegotiableParams()
    assert params == params2
    
    params2.protocol_version = "2.0.0"
    assert params != params2

def test_ranked_options():
    # Test RankedDataFormat
    opt1 = RankedDataFormat(DataFormat.VECTOR_FLOAT32, 1, True)
    opt2 = RankedDataFormat(DataFormat.VECTOR_INT8, 2, False)
    
    assert opt1.value == DataFormat.VECTOR_FLOAT32
    assert opt1.rank == 1
    assert opt1.required is True
    assert len(opt1.fallbacks) == 0
    
    # Test with fallbacks
    opt3 = RankedDataFormat(
        DataFormat.COMPRESSED_STATE, 3, False,
        [DataFormat.BINARY_CUSTOM, DataFormat.VECTOR_FLOAT32]
    )
    assert len(opt3.fallbacks) == 2
    
    # Test comparison
    assert opt1 < opt2  # Lower rank is preferred
    
    # Test RankedCompression
    comp1 = RankedCompression(CompressionAlgorithm.ZSTD, 1, True)
    comp2 = RankedCompression(CompressionAlgorithm.LZ4, 2, False)
    assert comp1 < comp2

def test_parameter_preference():
    pref = ParameterPreference()
    
    # Add ranked options
    pref.data_formats = [
        RankedDataFormat(DataFormat.VECTOR_FLOAT32, 1, True),
        RankedDataFormat(DataFormat.VECTOR_INT8, 2, False)
    ]
    
    pref.compression_algorithms = [
        RankedCompression(CompressionAlgorithm.ZSTD, 1, False),
        RankedCompression(CompressionAlgorithm.LZ4, 2, False)
    ]
    
    # Test optimal parameters generation
    params = pref.create_optimal_parameters()
    assert isinstance(params, NegotiableParams)
    
    # Test compatibility validation
    assert pref.validate_security_parameters(params)
    
    # Test compatibility score
    score = pref.calculate_compatibility_score(params)
    assert isinstance(score, int)
    assert score >= 0

def test_negotiation_protocol():
    protocol = NegotiationProtocol()
    params = NegotiableParams()
    
    # Configure secure parameters
    params.encryption_algorithm = EncryptionAlgorithm.AES_GCM
    params.key_exchange_method = KeyExchangeMethod.ECDH_X25519
    params.authentication_method = AuthenticationMethod.HMAC_SHA256
    params.key_size = KeySize.BITS_256
    
    # Test session initiation
    session_id = protocol.initiate_session("test_agent", params)
    assert isinstance(session_id, str)
    
    # Test session state retrieval
    state = protocol.get_session_state(session_id)
    assert isinstance(state, int)  # Assuming enum is mapped to int
    
    # Test negotiated parameters retrieval
    negotiated_params = protocol.get_negotiated_params(session_id)
    if negotiated_params:
        assert isinstance(negotiated_params, NegotiableParams)
    
    # Test counter proposal handling
    assert isinstance(protocol.accept_counter_proposal(session_id), bool)
    assert isinstance(protocol.reject_counter_proposal(session_id, "Not compatible"), bool)
    
    # Test session cleanup
    assert protocol.close_session(session_id) 