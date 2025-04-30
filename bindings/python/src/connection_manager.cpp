#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>
#include <pybind11/stl.h>
#include "xenocomm/core/connection_manager.hpp"
#include "type_converters.hpp"

namespace py = pybind11;
using namespace xenocomm::core;
using namespace xenocomm;

// Specialized type converters for ConnectionManager types
template<>
py::object TypeConverter::to_python<ConnectionConfig>(const ConnectionConfig& cfg) {
    py::dict result;
    result["timeout"] = cfg.timeout;
    result["auto_reconnect"] = cfg.autoReconnect;
    result["max_retries"] = cfg.maxRetries;
    result["retry_delay"] = cfg.retryDelay;
    return result;
}

template<>
ConnectionConfig TypeConverter::from_python<ConnectionConfig>(const py::object& obj) {
    py::dict dict = obj.cast<py::dict>();
    ConnectionConfig cfg;
    cfg.timeout = dict["timeout"].cast<std::chrono::milliseconds>();
    cfg.autoReconnect = dict["auto_reconnect"].cast<bool>();
    cfg.maxRetries = dict["max_retries"].cast<int>();
    cfg.retryDelay = dict["retry_delay"].cast<std::chrono::milliseconds>();
    return cfg;
}

void init_connection_manager(py::module_& m) {
    // Bind ConnectionStatus enum
    py::enum_<ConnectionStatus>(m, "ConnectionStatus")
        .value("DISCONNECTED", ConnectionStatus::Disconnected)
        .value("CONNECTING", ConnectionStatus::Connecting)
        .value("CONNECTED", ConnectionStatus::Connected)
        .value("ERROR", ConnectionStatus::Error)
        .export_values();

    // Bind ConnectionConfig struct with improved type conversion
    py::class_<ConnectionConfig>(m, "ConnectionConfig")
        .def(py::init<>())
        .def(py::init([](const py::dict& dict) {
            return TypeConverter::from_python<ConnectionConfig>(dict);
        }))
        .def_readwrite("timeout", &ConnectionConfig::timeout)
        .def_readwrite("auto_reconnect", &ConnectionConfig::autoReconnect)
        .def_readwrite("max_retries", &ConnectionConfig::maxRetries)
        .def_readwrite("retry_delay", &ConnectionConfig::retryDelay)
        .def("to_dict", [](const ConnectionConfig& cfg) {
            return TypeConverter::to_python(cfg);
        })
        .def("__repr__",
            [](const ConnectionConfig& cfg) {
                return "ConnectionConfig(timeout=" + std::to_string(cfg.timeout.count()) + "ms, "
                    "auto_reconnect=" + std::string(cfg.autoReconnect ? "True" : "False") + ", "
                    "max_retries=" + std::to_string(cfg.maxRetries) + ", "
                    "retry_delay=" + std::to_string(cfg.retryDelay.count()) + "ms)";
            });

    // Bind Connection class with shared_ptr support
    py::class_<Connection, std::shared_ptr<Connection>>(m, "Connection")
        .def(py::init([](Connection::ConnectionId id, const py::object& config) {
            ConnectionConfig cfg;
            if (!config.is_none()) {
                cfg = TypeConverter::from_python<ConnectionConfig>(config);
            }
            return std::make_shared<Connection>(id, cfg);
        }), py::arg("id"), py::arg("config") = py::none())
        .def("get_id", &Connection::getId)
        .def("get_status", &Connection::getStatus)
        .def("get_config", [](const Connection& conn) {
            return TypeConverter::to_python(conn.getConfig());
        })
        .def("__repr__",
            [](const Connection& conn) {
                return "Connection(id='" + conn.getId() + "', status=" + 
                    std::to_string(static_cast<int>(conn.getStatus())) + ")";
            });

    // Register shared_ptr conversion for Connection
    register_shared_ptr_conversion<Connection>(m);

    // Bind ConnectionManager class with improved memory management
    py::class_<ConnectionManager, std::shared_ptr<ConnectionManager>>(m, "ConnectionManager")
        .def(py::init([]() {
            return std::make_shared<ConnectionManager>();
        }))
        .def("establish", [](ConnectionManager& self, const std::string& connection_id, const py::object& config) {
            ConnectionConfig cfg;
            if (!config.is_none()) {
                cfg = TypeConverter::from_python<ConnectionConfig>(config);
            }
            return self.establish(connection_id, cfg);
        }, py::arg("connection_id"), py::arg("config") = py::none(),
           "Establish a new connection with the given ID and configuration")
        .def("close", &ConnectionManager::close,
            py::arg("connection_id"),
            "Close an existing connection")
        .def("check_status", &ConnectionManager::checkStatus,
            py::arg("connection_id"),
            "Check the status of a connection")
        .def("get_connection", &ConnectionManager::getConnection,
            py::arg("connection_id"),
            "Get an existing connection by ID")
        .def("get_active_connections", [](const ConnectionManager& self) {
            return vector_to_list<std::shared_ptr<Connection>>(self.getActiveConnections());
        }, "Get all active connections")
        .def("__enter__", [](ConnectionManager& self) { return &self; })
        .def("__exit__",
            [](ConnectionManager& self, py::object exc_type, py::object exc_value, py::object traceback) {
                // Close all active connections on context manager exit
                auto connections = self.getActiveConnections();
                for (const auto& conn : connections) {
                    self.close(conn->getId());
                }
            })
        .def("__repr__",
            [](const ConnectionManager& mgr) {
                auto conns = mgr.getActiveConnections();
                return "ConnectionManager(active_connections=" + std::to_string(conns.size()) + ")";
            });

    // Register shared_ptr conversion for ConnectionManager
    register_shared_ptr_conversion<ConnectionManager>(m);
} 