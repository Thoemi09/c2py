// Copyright (c) 2017-2018 Commissariat à l'énergie atomique et aux énergies alternatives (CEA)
// Copyright (c) 2017-2018 Centre national de la recherche scientifique (CNRS)
// Copyright (c) 2018-2022 Simons Foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0.txt
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Authors: Olivier Parcollet, Nils Wentzell

#include "./pyref.hpp"

#include <ios>
#include <iostream>
#include <sstream>
#include "./util/macros.hpp"

namespace c2py {

  bool check_python_version(const char *module_name, long version_major, long version_minor) {

    std::stringstream out;

    // Check that the python version of Python.h used to:
    //    -  compile the module including c2py.hpp
    //          (arguments of this function and frozen at compile time of the module).
    //    -  compile this file, hence libc2py.
    //          (PY_MAJOR_VERSION and PY_MINOR_VERSION below, determined by the Python.h used to compile this file)
    //  are identical.
    if (version_major != PY_MAJOR_VERSION or version_minor != PY_MINOR_VERSION) {
      out << "\n\n  Can not load the c2py module "                          //
          << (module_name ? module_name : "") << " ! \n\n"                  //
          << "    The c2py library was compiled with Python version "       //
          << PY_MAJOR_VERSION << '.' << PY_MINOR_VERSION << "\n"            //
          << "    but the python extension is compiled with Python version" //
          << version_major << '.' << version_minor << "\n"                  //
          << "    They should be identical.\n";
    }

    // Check that the python version of :
    //    -  the interpreter currently running, picked up from the sys module at runtime.
    //    -  Python.h used to compile the module including c2py.hpp
    //          (arguments of this function and frozen at compile time of the module).
    //  are identical.
    auto sys_version_info = pyref::module("sys").attr("version_info");
    auto rt_version_major = PyLong_AsLong(sys_version_info.attr("major"));
    auto rt_version_minor = PyLong_AsLong(sys_version_info.attr("minor"));
    if (rt_version_major != PY_MAJOR_VERSION or rt_version_minor != PY_MINOR_VERSION) {
      out << "\n\n  Can not load the c2py module "                    //
          << (module_name ? module_name : "") << " ! \n\n"            //
          << "    The c2py library was compiled with Python version " //
          << PY_MAJOR_VERSION << '.' << PY_MINOR_VERSION << "\n"      //
          << "    but the python intepreter has version "             //
          << rt_version_major << '.' << rt_version_minor << "\n"      //
          << "    They should be identical.\n";
    }

    if (out.str().empty()) return true;
    PyErr_SetString(PyExc_ImportError, out.str().c_str());
    return false;
  }

  //-------------------

  pyref &pyref::operator=(pyref const &p) {
    Py_XDECREF(ob);
    ob = p.ob;
    Py_XINCREF(ob);
    return *this;
  }

  ///
  pyref &pyref::operator=(pyref &&p) noexcept {
    Py_XDECREF(ob);
    ob   = p.ob;
    p.ob = nullptr;
    return *this;
  }

  /// Import the module and returns a pyref to it
  pyref pyref::module(std::string const &module_name) {
    // Maybe the module was already imported?
    PyObject *mod = PyImport_GetModule(pyref::string(module_name.c_str()));

    // If not, import normally
    if (mod == nullptr) mod = PyImport_ImportModule(module_name.c_str());

    // Did we succeed?
    if (mod == nullptr) // throw std::runtime_error(std::string{"Failed to import module "} + module_name);
      PyErr_SetString(PyExc_ImportError, ("Can not import module " + module_name).c_str());

    return mod;
  }

  /// gets a reference to the class cls_name in module_name
  pyref pyref::get_class(const char *module_name, const char *cls_name, bool raise_exception) {
    pyref cls = pyref::module(module_name).attr(cls_name);
    if (cls.is_null() && raise_exception) {
      std::string s = std::string{"Cannot find "} + module_name + "." + cls_name;
      PyErr_SetString(PyExc_TypeError, s.c_str());
    }
    return cls;
  }

  /// checks that ob is of type module_name.cls_name
  bool pyref::check_is_instance(PyObject *ob, PyObject *cls, bool raise_exception) {
    int i = PyObject_IsInstance(ob, cls);
    if (i == -1) { // an error has occurred
      i = 0;
      if (!raise_exception) PyErr_Clear();
    }
    if ((i == 0) && (raise_exception)) {
      pyref cls_name_obj = PyObject_GetAttrString(cls, "__name__");
      std::string err    = "Type error. Expected ";
      err.append(PyUnicode_AsUTF8(cls_name_obj));
      PyErr_SetString(PyExc_TypeError, err.c_str());
    }
    return i;
  }

  /// String repr of a Python Object.
  std::string to_string(PyObject *ob) {
    assert(ob != nullptr);
    pyref py_str = PyObject_Str(ob);
    return PyUnicode_AsUTF8(py_str);
  }

  /// String repr of a Python Object.
  std::string to_string(PyTypeObject *ob) {
    return to_string((PyObject *)ob); //NOLINT
  }

  /// Get the error message of the current Python exception.
  std::string get_python_error() {

#if (PY_MAJOR_VERSION == 3) and (PY_MINOR_VERSION >= 12)
    c2py::pyref exc = PyErr_GetRaisedException();
    if (exc) {
      c2py::pyref exc_args = PyObject_GetAttrString(exc, "args");
      assert(exc_args);
      PyObject *arg0 = PyTuple_GetItem(exc_args, 0);
      if (PyUnicode_Check(arg0))
        return PyUnicode_AsUTF8(arg0);
      else
        return "<not utf string>";
    } else
      return {};
#else
    std::string r;
    PyObject *error_msg = nullptr, *ptype = nullptr, *ptraceback = nullptr;
    PyErr_Fetch(&ptype, &error_msg, &ptraceback);
    //PyObject_Print(ptype, stdout, Py_PRINT_RAW);
    //PyObject_Print(error_msg, stdout, Py_PRINT_RAW);
    //PyObject_Print(ptraceback, stdout, Py_PRINT_RAW);
    if (error_msg and PyUnicode_Check(error_msg)) r = PyUnicode_AsUTF8(error_msg);
    Py_XDECREF(ptype);
    Py_XDECREF(ptraceback);
    Py_XDECREF(error_msg);
    return r;
#endif
  }

} // namespace c2py
