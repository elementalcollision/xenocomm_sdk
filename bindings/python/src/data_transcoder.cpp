#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "xenocomm/core/data_transcoder.h"
#include "type_converters.hpp"
#include "xenocomm/core/base64_transcoder.h"

namespace py = pybind11;
using namespace xenocomm::core;
using namespace xenocomm;

void init_data_transcoder(py::module_& m) {
    // Bind DataFormat enum if not already bound in negotiation_protocol
    if (!py::hasattr(m, "DataFormat")) {
        py::enum_<DataFormat>(m, "DataFormat")
            .value("VECTOR_FLOAT32", DataFormat::VECTOR_FLOAT32)
            .value("VECTOR_INT8", DataFormat::VECTOR_INT8)
            .value("COMPRESSED_STATE", DataFormat::COMPRESSED_STATE)
            .value("BINARY_CUSTOM", DataFormat::BINARY_CUSTOM)
            .value("GGWAVE_FSK", DataFormat::GGWAVE_FSK)
            .export_values();
    }

    // Bind TranscodingError exception
    py::register_exception<TranscodingError>(m, "TranscodingError");

    // Bind TranscodingMetadata struct with improved type conversion
    py::class_<TranscodingMetadata>(m, "TranscodingMetadata")
        .def(py::init<>())
        .def_readwrite("format", &TranscodingMetadata::format)
        .def_property("dimensions",
            [](const TranscodingMetadata& meta) {
                return vector_to_list<size_t>(meta.dimensions);
            },
            [](TranscodingMetadata& meta, const py::list& dims) {
                meta.dimensions = list_to_vector<size_t>(dims);
            })
        .def_readwrite("scale_factor", &TranscodingMetadata::scale_factor)
        .def_readwrite("compression_algorithm", &TranscodingMetadata::compression_algorithm)
        .def_readwrite("version", &TranscodingMetadata::version)
        .def_readwrite("element_count", &TranscodingMetadata::element_count)
        .def_readwrite("element_size", &TranscodingMetadata::element_size)
        .def("__repr__",
            [](const TranscodingMetadata& meta) {
                return "TranscodingMetadata("
                    "format=" + std::to_string(static_cast<int>(meta.format)) + ", "
                    "dimensions=[" + [&]() {
                        std::string dims;
                        for (const auto& d : meta.dimensions) {
                            dims += std::to_string(d) + ", ";
                        }
                        return dims.empty() ? dims : dims.substr(0, dims.length() - 2);
                    }() + "], "
                    "scale_factor=" + std::to_string(meta.scale_factor) + ", "
                    "compression='" + meta.compression_algorithm + "', "
                    "version=" + std::to_string(meta.version) + ", "
                    "element_count=" + std::to_string(meta.element_count) + ", "
                    "element_size=" + std::to_string(meta.element_size) + ")";
            });

    // Register DataBuffer types for efficient data handling
    register_buffer<float>(m, "Float32Buffer");
    register_buffer<int8_t>(m, "Int8Buffer");

    // Bind DataTranscoder class with improved memory management and buffer protocol support
    py::class_<DataTranscoder, std::shared_ptr<DataTranscoder>>(m, "DataTranscoder")
        // Encode methods with buffer protocol support
        .def("encode_float32", [](DataTranscoder& self, py::buffer data) {
            py::buffer_info buf = data.request();
            if (buf.format != py::format_descriptor<float>::format()) {
                throw py::type_error("Expected a buffer of float32 values");
            }
            
            // Create a DataBuffer for efficient handling
            DataBuffer<float> buffer(
                std::vector<float>(
                    static_cast<float*>(buf.ptr),
                    static_cast<float*>(buf.ptr) + buf.size
                )
            );
            
            return self.encode(buffer.data(), buffer.size() * sizeof(float), DataFormat::VECTOR_FLOAT32);
        }, "Encode float32 buffer data")
        
        .def("encode_int8", [](DataTranscoder& self, py::buffer data) {
            py::buffer_info buf = data.request();
            if (buf.format != py::format_descriptor<int8_t>::format()) {
                throw py::type_error("Expected a buffer of int8 values");
            }
            
            DataBuffer<int8_t> buffer(
                std::vector<int8_t>(
                    static_cast<int8_t*>(buf.ptr),
                    static_cast<int8_t*>(buf.ptr) + buf.size
                )
            );
            
            return self.encode(buffer.data(), buffer.size(), DataFormat::VECTOR_INT8);
        }, "Encode int8 buffer data")
        
        .def("encode", [](DataTranscoder& self, py::bytes data, DataFormat format) {
            std::string str = static_cast<std::string>(data);
            return self.encode(str.data(), str.size(), format);
        }, py::arg("data"), py::arg("format"), "Encode raw bytes data")

        // Decode methods with buffer protocol support
        .def("decode_float32", [](DataTranscoder& self, const std::vector<uint8_t>& encoded_data) {
            auto decoded = self.decode(encoded_data, DataFormat::VECTOR_FLOAT32);
            auto metadata = self.getMetadata(encoded_data);
            
            std::vector<ssize_t> shape;
            if (!metadata.dimensions.empty()) {
                shape = std::vector<ssize_t>(metadata.dimensions.begin(), metadata.dimensions.end());
            } else {
                shape = {static_cast<ssize_t>(decoded.size() / sizeof(float))};
            }
            
            // Use DataBuffer for memory-efficient buffer protocol support
            auto buffer = std::make_unique<DataBuffer<float>>(
                std::vector<float>(
                    reinterpret_cast<float*>(decoded.data()),
                    reinterpret_cast<float*>(decoded.data() + decoded.size())
                )
            );
            
            return py::array_t<float>(
                shape,
                {sizeof(float)},  // Strides
                buffer->data(),
                py::capsule(buffer.release(), [](void* p) {
                    delete static_cast<DataBuffer<float>*>(p);
                })
            );
        }, "Decode data to float32 buffer")
        
        .def("decode_int8", [](DataTranscoder& self, const std::vector<uint8_t>& encoded_data) {
            auto decoded = self.decode(encoded_data, DataFormat::VECTOR_INT8);
            auto metadata = self.getMetadata(encoded_data);
            
            std::vector<ssize_t> shape;
            if (!metadata.dimensions.empty()) {
                shape = std::vector<ssize_t>(metadata.dimensions.begin(), metadata.dimensions.end());
            } else {
                shape = {static_cast<ssize_t>(decoded.size())};
            }
            
            // Use DataBuffer for memory-efficient buffer protocol support
            auto buffer = std::make_unique<DataBuffer<int8_t>>(
                std::vector<int8_t>(
                    reinterpret_cast<int8_t*>(decoded.data()),
                    reinterpret_cast<int8_t*>(decoded.data() + decoded.size())
                )
            );
            
            return py::array_t<int8_t>(
                shape,
                {sizeof(int8_t)},  // Strides
                buffer->data(),
                py::capsule(buffer.release(), [](void* p) {
                    delete static_cast<DataBuffer<int8_t>*>(p);
                })
            );
        }, "Decode data to int8 buffer")
        
        .def("decode", &DataTranscoder::decode,
            py::arg("encoded_data"),
            py::arg("source_format"),
            "Decode raw data bytes")

        // Utility methods with improved type safety
        .def("is_valid_format", [](DataTranscoder& self, py::buffer data, DataFormat format) {
            py::buffer_info buf = data.request();
            return self.isValidFormat(buf.ptr, buf.size * buf.itemsize, format);
        }, "Check if data format is valid")
        
        .def("get_metadata", &DataTranscoder::getMetadata,
            py::arg("encoded_data"),
            "Get metadata from encoded data");

    // Register shared_ptr conversion for DataTranscoder
    register_shared_ptr_conversion<DataTranscoder>(m);

    py::class_<Base64Transcoder, DataTranscoder, std::shared_ptr<Base64Transcoder>>(m, "Base64Transcoder")
        .def(py::init<>())
        .def("encode", [](Base64Transcoder& self, py::bytes input) {
            std::string s = input;
            auto out = self.encode(s.data(), s.size(), DataFormat::BINARY_CUSTOM);
            return py::bytes(reinterpret_cast<const char*>(out.data()), out.size());
        })
        .def("decode", [](Base64Transcoder& self, py::bytes input) {
            std::string s = input;
            std::vector<uint8_t> buf(s.begin(), s.end());
            auto out = self.decode(buf, DataFormat::BINARY_CUSTOM);
            return py::bytes(reinterpret_cast<const char*>(out.data()), out.size());
        })
        .def("name", &Base64Transcoder::name);
} 