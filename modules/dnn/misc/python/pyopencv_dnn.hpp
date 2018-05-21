#ifdef HAVE_OPENCV_DNN
typedef dnn::DictValue LayerId;
typedef std::vector<dnn::MatShape> vector_MatShape;
typedef std::vector<std::vector<dnn::MatShape> > vector_vector_MatShape;


template<>
bool pyopencv_to(PyObject *o, dnn::DictValue &dv, const char *name)
{
    (void)name;
    if (!o || o == Py_None)
        return true; //Current state will be used
    else if (PyLong_Check(o))
    {
        dv = dnn::DictValue((int64)PyLong_AsLongLong(o));
        return true;
    }
    else if (PyInt_Check(o))
    {
        dv = dnn::DictValue((int64)PyInt_AS_LONG(o));
        return true;
    }
    else if (PyFloat_Check(o))
    {
        dv = dnn::DictValue(PyFloat_AS_DOUBLE(o));
        return true;
    }
    else if (PyString_Check(o))
    {
        dv = dnn::DictValue(String(PyString_AsString(o)));
        return true;
    }
    else
        return false;
}

template<>
bool pyopencv_to(PyObject *o, std::vector<Mat> &blobs, const char *name) //required for Layer::blobs RW
{
  return pyopencvVecConverter<Mat>::to(o, blobs, ArgInfo(name, false));
}

template<typename T>
PyObject* pyopencv_from(const dnn::DictValue &dv)
{
    if (dv.size() > 1)
    {
        std::vector<T> vec(dv.size());
        for (int i = 0; i < dv.size(); ++i)
            vec[i] = dv.get<T>(i);
        return pyopencv_from_generic_vec(vec);
    }
    else
        return pyopencv_from(dv.get<T>());
}

template<>
PyObject* pyopencv_from(const dnn::DictValue &dv)
{
    if (dv.isInt()) return pyopencv_from<int>(dv);
    if (dv.isReal()) return pyopencv_from<float>(dv);
    if (dv.isString()) return pyopencv_from<String>(dv);
    CV_Error(Error::StsNotImplemented, "Unknown value type");
    return NULL;
}

template<>
PyObject* pyopencv_from(const dnn::LayerParams& lp)
{
    PyObject* dict = PyDict_New();
    for (std::map<String, dnn::DictValue>::const_iterator it = lp.begin(); it != lp.end(); ++it)
    {
        CV_Assert(!PyDict_SetItemString(dict, it->first.c_str(), pyopencv_from(it->second)));
    }
    return dict;
}

class pycvLayer CV_FINAL : public dnn::Layer
{
public:
    pycvLayer(const dnn::LayerParams &params, PyObject* pyLayer) : Layer(params)
    {
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();

        PyObject* args = PyTuple_New(2);
        CV_Assert(!PyTuple_SetItem(args, 0, pyopencv_from(params)));
        CV_Assert(!PyTuple_SetItem(args, 1, pyopencv_from(params.blobs)));
        o = PyObject_CallObject(pyLayer, args);

        Py_DECREF(args);
        PyGILState_Release(gstate);
        if (!o)
            CV_Error(Error::StsError, "Failed to create an instance of custom layer");
    }

    static void registerLayer(const std::string& type, PyObject* o)
    {
        std::map<std::string, std::vector<PyObject*> >::iterator it = pyLayers.find(type);
        if (it != pyLayers.end())
            it->second.push_back(o);
        else
            pyLayers[type] = std::vector<PyObject*>(1, o);
    }

    static void unregisterLayer(const std::string& type)
    {
        std::map<std::string, std::vector<PyObject*> >::iterator it = pyLayers.find(type);
        if (it != pyLayers.end())
        {
            if (it->second.size() > 1)
                it->second.pop_back();
            else
                pyLayers.erase(it);
        }
    }

    static Ptr<dnn::Layer> create(dnn::LayerParams &params)
    {
        std::map<std::string, std::vector<PyObject*> >::iterator it = pyLayers.find(params.type);
        if (it == pyLayers.end())
            CV_Error(Error::StsNotImplemented, "Layer with a type \"" + params.type +
                                               "\" is not implemented");
        CV_Assert(!it->second.empty());
        return Ptr<dnn::Layer>(new pycvLayer(params, it->second.back()));
    }

    virtual bool getMemoryShapes(const std::vector<std::vector<int> > &inputs,
                                 const int,
                                 std::vector<std::vector<int> > &outputs,
                                 std::vector<std::vector<int> > &) const CV_OVERRIDE
    {
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();

        PyObject* args = PyList_New(inputs.size());
        for(size_t i = 0; i < inputs.size(); ++i)
            PyList_SET_ITEM(args, i, pyopencv_from_generic_vec(inputs[i]));

        PyObject* res = PyObject_CallMethodObjArgs(o, PyString_FromString("getMemoryShapes"), args, NULL);
        Py_DECREF(args);
        PyGILState_Release(gstate);
        if (!res)
            CV_Error(Error::StsNotImplemented, "Failed to call \"getMemoryShapes\" method");
        pyopencv_to_generic_vec(res, outputs, ArgInfo("", 0));
        return false;
    }

    virtual void forward(std::vector<Mat*> &inputs, std::vector<Mat> &outputs, std::vector<Mat> &) CV_OVERRIDE
    {
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();

        std::vector<Mat> inps(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i)
            inps[i] = *inputs[i];

        PyObject* args = pyopencv_from(inps);
        PyObject* res = PyObject_CallMethodObjArgs(o, PyString_FromString("forward"), args, NULL);
        Py_DECREF(args);
        PyGILState_Release(gstate);
        if (!res)
            CV_Error(Error::StsNotImplemented, "Failed to call \"forward\" method");

        std::vector<Mat> pyOutputs;
        pyopencv_to(res, pyOutputs, ArgInfo("", 0));

        CV_Assert(pyOutputs.size() == outputs.size());
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            CV_Assert(pyOutputs[i].size == outputs[i].size);
            CV_Assert(pyOutputs[i].type() == outputs[i].type());
            pyOutputs[i].copyTo(outputs[i]);
        }
    }

    virtual void forward(InputArrayOfArrays, OutputArrayOfArrays, OutputArrayOfArrays) CV_OVERRIDE
    {
        CV_Error(Error::StsNotImplemented, "");
    }

private:
    // Map layers types to python classes.
    static std::map<std::string, std::vector<PyObject*> > pyLayers;
    PyObject* o;  // Instance of implemented python layer.
};

std::map<std::string, std::vector<PyObject*> > pycvLayer::pyLayers;

static PyObject *pyopencv_cv_dnn_registerLayer(PyObject*, PyObject *args, PyObject *kw)
{
    const char *keywords[] = { "type", "class", NULL };
    char* layerType;
    PyObject *classInstance;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "sO", (char**)keywords, &layerType, &classInstance))
        return NULL;
    if (!PyCallable_Check(classInstance)) {
        PyErr_SetString(PyExc_TypeError, "class must be callable");
        return NULL;
    }

    pycvLayer::registerLayer(layerType, classInstance);
    dnn::LayerFactory::registerLayer(layerType, pycvLayer::create);
    Py_RETURN_NONE;
}

static PyObject *pyopencv_cv_dnn_unregisterLayer(PyObject*, PyObject *args, PyObject *kw)
{
    const char *keywords[] = { "type", NULL };
    char* layerType;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "s", (char**)keywords, &layerType))
        return NULL;

    pycvLayer::unregisterLayer(layerType);
    dnn::LayerFactory::unregisterLayer(layerType);
    Py_RETURN_NONE;
}

#endif  // HAVE_OPENCV_DNN
