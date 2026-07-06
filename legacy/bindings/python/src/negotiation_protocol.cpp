#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>
#include "xenocomm/core/negotiation_protocol.h"

namespace py = pybind11;
using namespace xenocomm::core;

void init_negotiation_protocol(py::module& m) {
    // Bind enums
    py::enum_<DataFormat>(m, "DataFormat")
        .value("VECTOR_FLOAT32", DataFormat::VECTOR_FLOAT32)
        .value("VECTOR_INT8", DataFormat::VECTOR_INT8)
        .value("COMPRESSED_STATE", DataFormat::COMPRESSED_STATE)
        .value("BINARY_CUSTOM", DataFormat::BINARY_CUSTOM)
        .value("GGWAVE_FSK", DataFormat::GGWAVE_FSK)
        .export_values();

    py::enum_<CompressionAlgorithm>(m, "CompressionAlgorithm")
        .value("NONE", CompressionAlgorithm::NONE)
        .value("ZLIB", CompressionAlgorithm::ZLIB)
        .value("LZ4", CompressionAlgorithm::LZ4)
        .value("ZSTD", CompressionAlgorithm::ZSTD)
        .export_values();

    py::enum_<ErrorCorrectionScheme>(m, "ErrorCorrectionScheme")
        .value("NONE", ErrorCorrectionScheme::NONE)
        .value("CHECKSUM_ONLY", ErrorCorrectionScheme::CHECKSUM_ONLY)
        .value("REED_SOLOMON", ErrorCorrectionScheme::REED_SOLOMON)
        .export_values();

    py::enum_<EncryptionAlgorithm>(m, "EncryptionAlgorithm")
        .value("NONE", EncryptionAlgorithm::NONE)
        .value("AES_GCM", EncryptionAlgorithm::AES_GCM)
        .value("AES_CBC", EncryptionAlgorithm::AES_CBC)
        .value("CHACHA20_POLY1305", EncryptionAlgorithm::CHACHA20_POLY1305)
        .value("XCHACHA20_POLY1305", EncryptionAlgorithm::XCHACHA20_POLY1305)
        .export_values();

    py::enum_<KeyExchangeMethod>(m, "KeyExchangeMethod")
        .value("NONE", KeyExchangeMethod::NONE)
        .value("RSA", KeyExchangeMethod::RSA)
        .value("DH", KeyExchangeMethod::DH)
        .value("ECDH_P256", KeyExchangeMethod::ECDH_P256)
        .value("ECDH_P384", KeyExchangeMethod::ECDH_P384)
        .value("ECDH_X25519", KeyExchangeMethod::ECDH_X25519)
        .export_values();

    py::enum_<AuthenticationMethod>(m, "AuthenticationMethod")
        .value("NONE", AuthenticationMethod::NONE)
        .value("HMAC_SHA256", AuthenticationMethod::HMAC_SHA256)
        .value("HMAC_SHA512", AuthenticationMethod::HMAC_SHA512)
        .value("ED25519_SIGNATURE", AuthenticationMethod::ED25519_SIGNATURE)
        .value("RSA_SIGNATURE", AuthenticationMethod::RSA_SIGNATURE)
        .export_values();

    py::enum_<KeySize>(m, "KeySize")
        .value("BITS_128", KeySize::BITS_128)
        .value("BITS_192", KeySize::BITS_192)
        .value("BITS_256", KeySize::BITS_256)
        .value("BITS_384", KeySize::BITS_384)
        .value("BITS_512", KeySize::BITS_512)
        .export_values();

    // Bind NegotiableParams struct
    py::class_<NegotiableParams>(m, "NegotiableParams")
        .def(py::init<>())
        .def_readwrite("protocol_version", &NegotiableParams::protocolVersion)
        .def_readwrite("security_version", &NegotiableParams::securityVersion)
        .def_readwrite("data_format", &NegotiableParams::dataFormat)
        .def_readwrite("compression_algorithm", &NegotiableParams::compressionAlgorithm)
        .def_readwrite("error_correction", &NegotiableParams::errorCorrection)
        .def_readwrite("encryption_algorithm", &NegotiableParams::encryptionAlgorithm)
        .def_readwrite("key_exchange_method", &NegotiableParams::keyExchangeMethod)
        .def_readwrite("authentication_method", &NegotiableParams::authenticationMethod)
        .def_readwrite("key_size", &NegotiableParams::keySize)
        .def_readwrite("custom_parameters", &NegotiableParams::customParameters)
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def("__repr__",
            [](const NegotiableParams& params) {
                return "NegotiableParams("
                    "protocol_version='" + params.protocolVersion + "', "
                    "security_version='" + params.securityVersion + "', "
                    "data_format=" + std::to_string(static_cast<int>(params.dataFormat)) + ", "
                    "compression=" + std::to_string(static_cast<int>(params.compressionAlgorithm)) + ", "
                    "error_correction=" + std::to_string(static_cast<int>(params.errorCorrection)) + ", "
                    "encryption=" + std::to_string(static_cast<int>(params.encryptionAlgorithm)) + ", "
                    "key_exchange=" + std::to_string(static_cast<int>(params.keyExchangeMethod)) + ", "
                    "auth_method=" + std::to_string(static_cast<int>(params.authenticationMethod)) + ", "
                    "key_size=" + std::to_string(static_cast<int>(params.keySize)) + ")";
            });

    // Bind RankedOption template for each type
    py::class_<RankedOption<DataFormat>>(m, "RankedDataFormat")
        .def(py::init<DataFormat, uint8_t, bool>())
        .def(py::init<DataFormat, uint8_t, bool, std::vector<DataFormat>>())
        .def_readwrite("value", &RankedOption<DataFormat>::value)
        .def_readwrite("rank", &RankedOption<DataFormat>::rank)
        .def_readwrite("required", &RankedOption<DataFormat>::required)
        .def_readwrite("fallbacks", &RankedOption<DataFormat>::fallbacks)
        .def(py::self < py::self);

    py::class_<RankedOption<CompressionAlgorithm>>(m, "RankedCompression")
        .def(py::init<CompressionAlgorithm, uint8_t, bool>())
        .def(py::init<CompressionAlgorithm, uint8_t, bool, std::vector<CompressionAlgorithm>>())
        .def_readwrite("value", &RankedOption<CompressionAlgorithm>::value)
        .def_readwrite("rank", &RankedOption<CompressionAlgorithm>::rank)
        .def_readwrite("required", &RankedOption<CompressionAlgorithm>::required)
        .def_readwrite("fallbacks", &RankedOption<CompressionAlgorithm>::fallbacks)
        .def(py::self < py::self);

    // Similar bindings for other RankedOption types...

    // Bind ParameterPreference struct
    py::class_<ParameterPreference>(m, "ParameterPreference")
        .def(py::init<>())
        .def_readwrite("data_formats", &ParameterPreference::dataFormats)
        .def_readwrite("compression_algorithms", &ParameterPreference::compressionAlgorithms)
        .def_readwrite("error_correction_schemes", &ParameterPreference::errorCorrectionSchemes)
        .def_readwrite("encryption_algorithms", &ParameterPreference::encryptionAlgorithms)
        .def_readwrite("key_exchange_methods", &ParameterPreference::keyExchangeMethods)
        .def_readwrite("authentication_methods", &ParameterPreference::authenticationMethods)
        .def_readwrite("key_sizes", &ParameterPreference::keySizes)
        .def_readwrite("custom_parameters", &ParameterPreference::customParameters)
        .def("validate_security_parameters", &ParameterPreference::validateSecurityParameters)
        .def("create_optimal_parameters", &ParameterPreference::createOptimalParameters)
        .def("build_compatible_params_with_fallbacks", &ParameterPreference::buildCompatibleParamsWithFallbacks)
        .def("is_compatible_with_requirements", &ParameterPreference::isCompatibleWithRequirements)
        .def("calculate_compatibility_score", &ParameterPreference::calculateCompatibilityScore);

    // Bind NegotiationProtocol class
    py::class_<NegotiationProtocol>(m, "NegotiationProtocol")
        .def("initiate_session", &NegotiationProtocol::initiateSession,
            py::arg("target_agent_id"),
            py::arg("proposed_params"),
            "Initiate a negotiation session with a target agent")
        .def("respond_to_negotiation", &NegotiationProtocol::respondToNegotiation,
            py::arg("session_id"),
            py::arg("response_type"),
            py::arg("response_params") = py::none(),
            "Respond to a negotiation request")
        .def("finalize_session", &NegotiationProtocol::finalizeSession,
            py::arg("session_id"),
            "Finalize a negotiation session")
        .def("get_session_state", &NegotiationProtocol::getSessionState,
            py::arg("session_id"),
            "Get the current state of a negotiation session")
        .def("get_negotiated_params", &NegotiationProtocol::getNegotiatedParams,
            py::arg("session_id"),
            "Get the negotiated parameters for a session")
        .def("accept_counter_proposal", &NegotiationProtocol::acceptCounterProposal,
            py::arg("session_id"),
            "Accept a counter-proposal from the remote agent")
        .def("reject_counter_proposal", &NegotiationProtocol::rejectCounterProposal,
            py::arg("session_id"),
            py::arg("reason") = py::none(),
            "Reject a counter-proposal from the remote agent")
        .def("close_session", &NegotiationProtocol::closeSession,
            py::arg("session_id"),
            "Close a negotiation session");
} 