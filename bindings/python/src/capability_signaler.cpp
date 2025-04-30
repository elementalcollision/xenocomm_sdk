#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/version.h"
#include "type_converters.hpp"

namespace py = pybind11;
using namespace xenocomm::core;
using namespace xenocomm;

// Specialized type converters for Capability types
template<>
py::object TypeConverter::to_python<CapabilityVersion>(const CapabilityVersion& ver) {
    py::dict result;
    result["major"] = ver.major;
    result["minor"] = ver.minor;
    result["patch"] = ver.patch;
    return result;
}

template<>
CapabilityVersion TypeConverter::from_python<CapabilityVersion>(const py::object& obj) {
    py::dict dict = obj.cast<py::dict>();
    return CapabilityVersion(
        dict["major"].cast<uint16_t>(),
        dict["minor"].cast<uint16_t>(),
        dict["patch"].cast<uint16_t>()
    );
}

template<>
py::object TypeConverter::to_python<Capability>(const Capability& cap) {
    py::dict result;
    result["name"] = cap.name;
    result["version"] = TypeConverter::to_python(cap.version);
    result["parameters"] = map_to_dict(cap.parameters);
    result["is_deprecated"] = cap.is_deprecated;
    if (cap.deprecated_since) {
        result["deprecated_since"] = TypeConverter::to_python(*cap.deprecated_since);
    }
    if (cap.removal_version) {
        result["removal_version"] = TypeConverter::to_python(*cap.removal_version);
    }
    if (!cap.replacement_capability.empty()) {
        result["replacement_capability"] = cap.replacement_capability;
    }
    return result;
}

template<>
Capability TypeConverter::from_python<Capability>(const py::object& obj) {
    py::dict dict = obj.cast<py::dict>();
    Capability cap(
        dict["name"].cast<std::string>(),
        TypeConverter::from_python<Version>(dict["version"]),
        dict_to_map<std::string, std::string>(dict["parameters"].cast<py::dict>())
    );
    
    if (dict.contains("is_deprecated")) {
        cap.is_deprecated = dict["is_deprecated"].cast<bool>();
    }
    if (dict.contains("deprecated_since")) {
        cap.deprecated_since = TypeConverter::from_python<CapabilityVersion>(dict["deprecated_since"]);
    }
    if (dict.contains("removal_version")) {
        cap.removal_version = TypeConverter::from_python<CapabilityVersion>(dict["removal_version"]);
    }
    if (dict.contains("replacement_capability")) {
        cap.replacement_capability = dict["replacement_capability"].cast<std::string>();
    }
    return cap;
}

void init_capability_signaler(py::module_& m) {
    // Register type converters
    TypeConverter::register_converter<CapabilityVersion>(m);
    TypeConverter::register_converter<Capability>(m);

    // Bind CapabilityVersion struct with improved memory management
    py::class_<CapabilityVersion, std::shared_ptr<CapabilityVersion>>(m, "CapabilityVersion")
        .def(py::init<>())
        .def(py::init<uint16_t, uint16_t, uint16_t>(),
            py::arg("major"), py::arg("minor"), py::arg("patch"))
        .def_readwrite("major", &CapabilityVersion::major)
        .def_readwrite("minor", &CapabilityVersion::minor)
        .def_readwrite("patch", &CapabilityVersion::patch)
        .def("__eq__", &CapabilityVersion::operator==)
        .def("__lt__", &CapabilityVersion::operator<)
        .def("__repr__",
            [](const CapabilityVersion& ver) {
                return "CapabilityVersion(" + std::to_string(ver.major) + "." +
                    std::to_string(ver.minor) + "." + std::to_string(ver.patch) + ")";
            });

    // Bind Capability struct with improved memory management
    py::class_<Capability, std::shared_ptr<Capability>>(m, "Capability")
        .def(py::init<>())
        .def(py::init<std::string, Version, std::map<std::string, std::string>>(),
            py::arg("name"), py::arg("version"), py::arg("parameters") = std::map<std::string, std::string>{})
        .def_property("name", 
            [](const Capability& cap) { return cap.name; },
            [](Capability& cap, const std::string& name) { cap.name = name; })
        .def_property("version",
            [](const Capability& cap) { return TypeConverter::to_python(cap.version); },
            [](Capability& cap, const py::object& ver) { cap.version = TypeConverter::from_python<Version>(ver); })
        .def_property("parameters",
            [](const Capability& cap) { return map_to_dict(cap.parameters); },
            [](Capability& cap, const py::dict& params) { cap.parameters = dict_to_map<std::string, std::string>(params); })
        .def_readwrite("is_deprecated", &Capability::is_deprecated)
        .def_property("deprecated_since",
            [](const Capability& cap) { return cap.deprecated_since ? TypeConverter::to_python(*cap.deprecated_since) : py::none(); },
            [](Capability& cap, const py::object& ver) { 
                cap.deprecated_since = ver.is_none() ? std::nullopt : std::make_optional(TypeConverter::from_python<CapabilityVersion>(ver)); 
            })
        .def_property("removal_version",
            [](const Capability& cap) { return cap.removal_version ? TypeConverter::to_python(*cap.removal_version) : py::none(); },
            [](Capability& cap, const py::object& ver) { 
                cap.removal_version = ver.is_none() ? std::nullopt : std::make_optional(TypeConverter::from_python<CapabilityVersion>(ver)); 
            })
        .def_readwrite("replacement_capability", &Capability::replacement_capability)
        .def("deprecate", &Capability::deprecate,
            py::arg("since"),
            py::arg("removal") = py::none(),
            py::arg("replacement") = py::none(),
            "Mark this capability as deprecated")
        .def("matches", &Capability::matches,
            py::arg("required"),
            py::arg("allow_partial") = false,
            "Check if this capability matches another capability's requirements")
        .def("__eq__", &Capability::operator==)
        .def("__lt__", &Capability::operator<)
        .def("__repr__",
            [](const Capability& cap) {
                return TypeConverter::to_python(cap).attr("__repr__")().cast<std::string>();
            });

    // Bind CapabilitySignaler class with improved memory management
    py::class_<CapabilitySignaler, std::shared_ptr<CapabilitySignaler>>(m, "CapabilitySignaler")
        .def(py::init<>())
        .def("register_capability", 
            [](CapabilitySignaler& self, const std::string& agent_id, const py::object& capability) {
                self.registerCapability(agent_id, TypeConverter::from_python<Capability>(capability));
            },
            py::arg("agent_id"),
            py::arg("capability"),
            "Register a capability for a specific agent")
        .def("unregister_capability",
            [](CapabilitySignaler& self, const std::string& agent_id, const py::object& capability) {
                self.unregisterCapability(agent_id, TypeConverter::from_python<Capability>(capability));
            },
            py::arg("agent_id"),
            py::arg("capability"),
            "Unregister a capability for an agent")
        .def("discover_agents",
            [](CapabilitySignaler& self, const py::list& required_capabilities) {
                return vector_to_list(self.discoverAgents(list_to_vector<Capability>(required_capabilities)));
            },
            py::arg("required_capabilities"),
            "Discover agents with exact capability matching")
        .def("discover_agents",
            [](CapabilitySignaler& self, const py::list& required_capabilities, bool partial_match) {
                return vector_to_list(self.discoverAgents(list_to_vector<Capability>(required_capabilities), partial_match));
            },
            py::arg("required_capabilities"),
            py::arg("partial_match"),
            "Discover agents with optional partial capability matching")
        .def("get_agent_capabilities",
            [](CapabilitySignaler& self, const std::string& agent_id) {
                return vector_to_list(self.getAgentCapabilities(agent_id));
            },
            py::arg("agent_id"),
            "Get all capabilities for an agent")
        .def("register_capability_binary",
            [](CapabilitySignaler& self, const std::string& agent_id, const py::bytes& capability_data) {
                std::string data = capability_data.cast<std::string>();
                self.registerCapabilityBinary(agent_id, std::vector<uint8_t>(data.begin(), data.end()));
            },
            py::arg("agent_id"),
            py::arg("capability_data"),
            "Register a capability using binary data")
        .def("get_agent_capabilities_binary",
            [](CapabilitySignaler& self, const std::string& agent_id) {
                auto data = self.getAgentCapabilitiesBinary(agent_id);
                return py::bytes(std::string(data.begin(), data.end()));
            },
            py::arg("agent_id"),
            "Get agent capabilities as binary data");
} 