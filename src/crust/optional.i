%include stdint.i

namespace std {
    template<class T> struct optional;


    %template() optional<string>;
    %template() optional<int16_t>;
    %template() optional<int32_t>;
    %template() optional<int64_t>;
    %template() optional<float>;
    %template() optional<double>;
}

%define %std_optional(TYPE)
%typemap(out) std::optional<TYPE> {
    if (!($1)) {
        #ifdef SWIGPYTHON
        $result = SWIG_Py_Void();
        #elif defined(SWIGR)
        $result = R_NilValue;
        #endif
    }
    else {
        $result = SWIG_NewPointerObj(new TYPE(($1).value()), $descriptor(TYPE *), SWIG_POINTER_OWN);
    }    
}
%template() std::optional<TYPE>;
%enddef

%typemap(in) std::optional<double> {
  $1 = PyFloat_AsDouble($input);
}

%typemap(out) std::optional<double>, std::optional<float> {
    if (!($1).has_value()) {
        #ifdef SWIGPYTHON
        $result = SWIG_Py_Void();
        #elif defined(SWIGR)
        $result = R_NilValue;
        #endif
    }
    else {
        $result = SWIG_From_double(($1).value());
    }
}

%typemap(out) std::optional<int64_t>, std::optional<int32_t>, std::optional<int16_t> {
    if (!($1).has_value()) {
        #ifdef SWIGPYTHON
        $result = SWIG_Py_Void();
        #elif defined(SWIGR)
        $result = R_NilValue;
        #endif
    }
    else {
        $result = SWIG_From_long(($1).value());
    }
}


%typemap(out) std::optional<std::string> {
    if (!($1).has_value()) {
        #ifdef SWIGPYTHON
        $result = SWIG_Py_Void();
        #elif defined(SWIGR)
        $result = R_NilValue;
        #endif
    }
    else {
        $result = SWIG_From_std_string($1.value());
    }
}
