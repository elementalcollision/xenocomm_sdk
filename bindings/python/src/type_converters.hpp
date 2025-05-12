#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <vector>
#include <map>
#include <memory>
#include <string>

namespace xenocomm {
namespace py = pybind11;

// Vector conversion helpers
template<typename T>
py::list vector_to_list(const std::vector<T>& vec) {
    py::list result;
    for (const auto& item : vec) {
        result.append(item);
    }
    return result;
}

template<typename T>
std::vector<T> list_to_vector(const py::list& list) {
    std::vector<T> result;
    result.reserve(list.size());
    for (const auto& item : list) {
        result.push_back(item.cast<T>());
    }
    return result;
}

// Map conversion helpers
template<typename K, typename V>
py::dict map_to_dict(const std::map<K, V>& map) {
    py::dict result;
    for (const auto& [key, value] : map) {
        result[py::cast(key)] = py::cast(value);
    }
    return result;
}

template<typename K, typename V>
std::map<K, V> dict_to_map(const py::dict& dict) {
    std::map<K, V> result;
    for (const auto& item : dict) {
        result.emplace(
            item.first.cast<K>(),
            item.second.cast<V>()
        );
    }
    return result;
}

// Buffer protocol support for raw data buffers
// Moved from type_converters.cpp

template<typename T>
class DataBuffer {
public:
    explicit DataBuffer(std::vector<T> data) : data_(std::move(data)) {}
    
    T* data() { return data_.data(); }
    const T* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }
    
private:
    std::vector<T> data_;
};

// Register buffer protocol support for a data type
// Moved from type_converters.cpp

template<typename T>
py::class_<DataBuffer<T>> register_buffer(py::module_& m, const char* name) {
    return py::class_<DataBuffer<T>>(m, name)
        .def(py::init<std::vector<T>>())
        .def_buffer([](DataBuffer<T>& buffer) -> py::buffer_info {
            return py::buffer_info(
                buffer.data(),                          // Pointer to buffer
                sizeof(T),                              // Size of one element
                py::format_descriptor<T>::format(),     // Python struct-style format descriptor
                1,                                      // Number of dimensions
                { buffer.size() },                      // Buffer dimensions
                { sizeof(T) }                           // Strides (in bytes) for each dimension
            );
        });
}

// Smart pointer integration with Python reference counting
template<typename T>
void register_shared_ptr_conversion(py::module_& m, const char* name = "_SharedPtr") {
    py::class_<std::shared_ptr<T>>(m, name)
        .def("use_count", &std::shared_ptr<T>::use_count);
        
    py::implicitly_convertible<py::object, std::shared_ptr<T>>();
}

// Custom deleter that coordinates with Python's GC
template<typename T>
struct py_deleter {
    py::object py_obj;
    
    explicit py_deleter(py::object obj) : py_obj(std::move(obj)) {}
    
    void operator()(T* ptr) {
        // Release Python reference when C++ object is deleted
        py_obj.dec_ref();
        delete ptr;
    }
};

// Type conversion base class for custom types
class TypeConverter {
public:
    template<typename T>
    static py::object to_python(const T& cpp_obj) {
        return py::cast(cpp_obj);
    }
    
    template<typename T>
    static T from_python(const py::object& py_obj) {
        return py_obj.cast<T>();
    }
};

// Declaration for type conversion system initialization
void init_type_converters(py::module_& m);

} // namespace xenocomm 