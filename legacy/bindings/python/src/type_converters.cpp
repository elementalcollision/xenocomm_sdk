#include "type_converters.hpp"
#include <pybind11/buffer_info.h>
#include <pybind11/numpy.h>

namespace xenocomm {

// Initialize type conversion system
void init_type_converters(py::module_& m) {
    // Register common buffer types
    register_buffer<uint8_t>(m, "ByteBuffer");
    register_buffer<float>(m, "FloatBuffer");
    register_buffer<double>(m, "DoubleBuffer");
    
    // Add numpy array support
    py::class_<std::vector<double>>(m, "DoubleVector")
        .def(py::init<>())
        .def("__len__", [](const std::vector<double>& v) { return v.size(); })
        .def("__iter__", [](std::vector<double>& v) {
            return py::make_iterator(v.begin(), v.end());
        }, py::keep_alive<0, 1>())
        .def("__getitem__", [](const std::vector<double>& v, size_t i) {
            if (i >= v.size()) throw py::index_error();
            return v[i];
        })
        .def("__setitem__", [](std::vector<double>& v, size_t i, double val) {
            if (i >= v.size()) throw py::index_error();
            v[i] = val;
        })
        .def_buffer([](std::vector<double>& v) -> py::buffer_info {
            return py::buffer_info(
                v.data(),
                sizeof(double),
                py::format_descriptor<double>::format(),
                1,
                { v.size() },
                { sizeof(double) }
            );
        });
}

} // namespace xenocomm 