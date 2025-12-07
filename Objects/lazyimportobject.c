/* Lazy object implementation */

#include "Python.h"
#include "pycore_ceval.h"
#include "pycore_frame.h"
#include "pycore_import.h"
#include "pycore_interpframe.h"
#include "pycore_lazyimportobject.h"

PyObject *
_PyLazyImport_New(PyObject *builtins, PyObject *from, PyObject *attr)
{
    PyLazyImportObject *m;
    if (!from || !PyUnicode_Check(from)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (attr == Py_None) {
        attr = NULL;
    }
    assert(!attr || PyObject_IsTrue(attr));
    m = PyObject_GC_New(PyLazyImportObject, &PyLazyImport_Type);
    if (m == NULL) {
        return NULL;
    }
    m->lz_builtins = Py_XNewRef(builtins);
    m->lz_from = Py_NewRef(from);
    m->lz_attr = Py_XNewRef(attr);

    /* Capture frame information for the original import location */
    m->lz_code = NULL;
    m->lz_instr_offset = -1;

    _PyInterpreterFrame *frame = _PyEval_GetFrame();
    if (frame != NULL) {
        PyCodeObject *code = _PyFrame_GetCode(frame);
        if (code != NULL) {
            m->lz_code = (PyCodeObject *)Py_NewRef(code);
            /* Calculate the instruction offset from the current frame */
            m->lz_instr_offset = _PyInterpreterFrame_LASTI(frame);
        }
    }

    _PyObject_GC_TRACK(m);
    return (PyObject *)m;
}

static int
lazy_import_traverse(PyLazyImportObject *m, visitproc visit, void *arg)
{
    Py_VISIT(m->lz_builtins);
    Py_VISIT(m->lz_from);
    Py_VISIT(m->lz_attr);
    Py_VISIT(m->lz_code);
    return 0;
}

static int
lazy_import_clear(PyLazyImportObject *m)
{
    Py_CLEAR(m->lz_builtins);
    Py_CLEAR(m->lz_from);
    Py_CLEAR(m->lz_attr);
    Py_CLEAR(m->lz_code);
    return 0;
}

static void
lazy_import_dealloc(PyLazyImportObject *m)
{
    _PyObject_GC_UNTRACK(m);
    lazy_import_clear(m);
    Py_TYPE(m)->tp_free((PyObject *)m);
}

static PyObject *
lazy_import_name(PyLazyImportObject *m)
{
    if (m->lz_attr != NULL) {
        if (PyUnicode_Check(m->lz_attr)) {
            return PyUnicode_FromFormat("%U.%U", m->lz_from, m->lz_attr);
        } else {
            return PyUnicode_FromFormat("%U...", m->lz_from);
        }
    }
    Py_INCREF(m->lz_from);
    return m->lz_from;
}

static PyObject *
lazy_import_repr(PyLazyImportObject *m)
{
    PyObject *name = lazy_import_name(m);
    if (name == NULL) {
        return NULL;
    }
    PyObject *res = PyUnicode_FromFormat("<lazy_import '%U'>", name);
    Py_DECREF(name);
    return res;
}

static PyObject *
lazy_import_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    if (PyTuple_GET_SIZE(args) != 2 && PyTuple_GET_SIZE(args) != 3) {
        PyErr_SetString(PyExc_ValueError, "lazy_import expected 2-3 arguments");
        return NULL;
    }

    PyObject *builtins = PyTuple_GET_ITEM(args, 0);
    PyObject *from = PyTuple_GET_ITEM(args, 1);
    PyObject *attr = NULL;
    if (PyTuple_GET_SIZE(args) == 3) {
        attr = PyTuple_GET_ITEM(args, 2);
    }

    return _PyLazyImport_New(builtins, from, attr);
}

PyObject *
_PyLazyImport_GetName(PyObject *lazy_import)
{
    assert(PyLazyImport_CheckExact(lazy_import));
    return lazy_import_name((PyLazyImportObject *)lazy_import);
}

static PyObject *
lazy_import_resolve(PyObject *self, PyObject *args)
{
    return _PyImport_LoadLazyImportTstate(PyThreadState_GET(), self);
}

static PyMethodDef lazy_import_methods[] = {
    {
        "resolve", lazy_import_resolve, METH_NOARGS,
        PyDoc_STR("resolves the lazy import and returns the actual object")
    },
    {NULL, NULL}
};


PyDoc_STRVAR(lazy_import_doc,
"lazy_import(builtins, name, fromlist=None, /)\n"
"--\n"
"\n"
"Represents a deferred import that will be resolved on first use.\n"
"\n"
"This type is used internally by the 'lazy import' statement.\n"
"Users should not typically create instances directly.");

PyTypeObject PyLazyImport_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "lazy_import",
    .tp_basicsize = sizeof(PyLazyImportObject),
    .tp_dealloc = (destructor)lazy_import_dealloc,
    .tp_repr = lazy_import_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE,
    .tp_doc = lazy_import_doc,
    .tp_traverse = (traverseproc)lazy_import_traverse,
    .tp_clear = (inquiry)lazy_import_clear,
    .tp_methods = lazy_import_methods,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = lazy_import_new,
    .tp_free = PyObject_GC_Del,
};
