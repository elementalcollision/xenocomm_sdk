#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "type_converters.hpp"

namespace py = pybind11;

// Forward declarations of binding functions
void init_connection_manager(py::module_& m);
void init_capability_signaler(py::module_& m);
void init_negotiation_protocol(py::module_& m);
void init_data_transcoder(py::module_& m);
void init_transmission_manager(py::module_& m);
void init_feedback_loop(py::module_& m);

PYBIND11_MODULE(_core, m) {
    m.doc() = "XenoComm SDK Python bindings"; // Module docstring

    // Initialize type conversion system first
    xenocomm::init_type_converters(m);

    // Initialize submodules
    init_connection_manager(m);
    init_capability_signaler(m);
    init_negotiation_protocol(m);
    init_data_transcoder(m);
    init_transmission_manager(m);
    init_feedback_loop(m);
} 