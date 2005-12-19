#include <Python.h>
#include <compile.h>
#include <frameobject.h>
#include <eval.h>


static PyObject* copyrec(PyObject* o);  /* forward */


static PyObject* genbuild(PyObject* g)
{
  PyObject* x;
  PyFrameObject* f;
  PyCodeObject* co;
  PyObject** dummy;
  int i, res;
  
  x = PyObject_GetAttrString(g, "gi_running");
  if (x == NULL)
    return NULL;
  res = PyObject_IsTrue(x);
  if (res < 0)
    return NULL;
  if (res) {
    PyErr_SetString(PyExc_ValueError, "generator is running");
    return NULL;
  }

  x = PyObject_GetAttrString(g, "gi_frame");
  if (x == NULL)
    return NULL;
  if (!PyFrame_Check(x)) {
    PyErr_SetString(PyExc_TypeError, "g.gi_frame must be a frame object");
    return NULL;
  }
  f = (PyFrameObject*) x;
  co = f->f_code;

  if (!(co->co_flags & CO_GENERATOR)) {
    PyErr_SetString(PyExc_ValueError, "the frame is not from a generator");
    return NULL;
  }
  if (f->f_stacktop == NULL) {
    Py_INCREF(g);  /* exhausted -- can return 'g' itself */
    return g;
  }
  if (f->f_nfreevars || f->f_ncells) {
    PyErr_SetString(PyExc_ValueError, "generator has cell or free vars");
    return NULL;
  }

  dummy = (PyObject**) malloc(co->co_argcount * sizeof(PyObject*));
  if (dummy == NULL)
    return PyErr_NoMemory();
  for (i=0; i<co->co_argcount; i++)
    dummy[i] = Py_None;
  x = PyEval_EvalCodeEx(co, f->f_globals, f->f_locals,
                        dummy, co->co_argcount, NULL, 0,
                        NULL, 0, NULL);
  free(dummy);
  return x;
}

static int gencopy(PyObject* g2, PyObject* g)
{
  PyObject* x;
  PyFrameObject* f;
  PyFrameObject* f2;
  PyCodeObject* co;
  int i, res;

  if (g != g2)
    {
      if (g2->ob_type != g->ob_type)
        {
          PyErr_SetString(PyExc_TypeError, "type mismatch");
          return -1;
        }

      x = PyObject_GetAttrString(g, "gi_frame");
      if (x == NULL)
        return -1;
      if (!PyFrame_Check(x)) {
        PyErr_SetString(PyExc_TypeError, "g.gi_frame must be a frame object");
        return -1;
      }
      f = (PyFrameObject*) x;
      co = f->f_code;

      x = PyObject_GetAttrString(g2, "gi_frame");
      if (x == NULL)
        return -1;
      if (!PyFrame_Check(x)) {
        PyErr_SetString(PyExc_TypeError, "returned gi_frame");
        return -1;
      }
      f2 = (PyFrameObject*) x;
      if (f2->f_stacksize != f->f_stacksize) {
        PyErr_SetString(PyExc_TypeError, "stack size mismatch");
        return -1;
      }

      if (f2->f_stacktop != NULL)
        while (f2->f_stacktop != f2->f_localsplus)
          {
            f2->f_stacktop--;
            Py_XDECREF(*f2->f_stacktop);
          }
      
      res = f->f_stacktop - f->f_localsplus;
      f2->f_lasti = f->f_lasti;
      f2->f_iblock = f->f_iblock;
      memcpy(f2->f_blockstack, f->f_blockstack, sizeof(PyTryBlock)*f->f_iblock);
      f2->f_stacktop = f2->f_localsplus;
      for (i=0; i<res; i++)
        {
          x = f->f_localsplus[i];
          if (x != NULL)
            x = copyrec(x);
          *f2->f_stacktop++ = x;
        }
    }
  return 0;
}


typedef struct {
	PyObject_HEAD
	long      it_index;
	PyObject *it_seq; /* Set to NULL when iterator is exhausted */
} seqiterobject;

static PyObject* seqiterbuild(PyObject* o)
{
  seqiterobject* iter = (seqiterobject*) o;
  if (iter->it_seq == NULL)
    {
      Py_INCREF(iter);  /* exhausted */
      return (PyObject*) iter;
    }
  else
    return PySeqIter_New(iter->it_seq);
}

static int seqitercopy(PyObject* o2, PyObject* o)
{
  PyObject* x;
  seqiterobject* iter  = (seqiterobject*) o;
  seqiterobject* iter2 = (seqiterobject*) o2;

  iter2->it_index = iter->it_index;
  if (iter->it_seq != NULL)
    {
      x = copyrec(iter->it_seq);
      Py_XDECREF(iter2->it_seq);
      iter2->it_seq = x;
    }
  return 0;
}


/* HACKS HACKS HACKS */

typedef struct {
  PyObject_HEAD
  PyObject* o;
} KeyObject;

#define KEYS_BY_BLOCK  1024

struct key_block {
  KeyObject keys[KEYS_BY_BLOCK];
  struct key_block* next;
};

static long key_hash(KeyObject* k)
{
  return (long)(k->o);
}

static PyObject* key_richcmp(KeyObject* k1, KeyObject* k2, int op)
{
  PyObject* r;
  assert(op == 2 /*PyCmp_EQ*/ );
  r = k1->o == k2->o ? Py_True : Py_False;
  Py_INCREF(r);
  return r;
}

static PyTypeObject keytype = {
	PyObject_HEAD_INIT(NULL)
	0,
	"key",
	sizeof(KeyObject),
	0,
	0, 			               /* tp_dealloc */
	0,                                      /* tp_print */
	0,	                                /* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	0,					/* tp_repr */
	0,					/* tp_as_number */
	0,			                /* tp_as_sequence */
	0,					/* tp_as_mapping */
	(hashfunc)key_hash,			/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	0,					/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,			/* tp_flags */
 	0,					/* tp_doc */
 	0,					/* tp_traverse */
 	0,					/* tp_clear */
        (richcmpfunc)key_richcmp,		/* tp_richcompare */
};


/* global state */
static PyObject* ss_memo;
static struct key_block* ss_block;
static int ss_next_in_block;

static PyObject* str_inst_build;
static PyTypeObject* GeneratorType;

static PyObject* copyrec(PyObject* o)
{
  PyTypeObject* t;
  PyObject* n;
  PyObject* key;
  KeyObject* fkey;

  if (o == Py_None || o->ob_type == &PyInt_Type || o->ob_type == &PyString_Type)
    {
      Py_INCREF(o);
      return o;
    }
  if (ss_next_in_block < 0)
    {
      struct key_block* b = (struct key_block*) malloc(sizeof(struct key_block));
      if (!b) { PyErr_NoMemory(); goto fail1; }
      b->next = ss_block;
      ss_block = b;
      ss_next_in_block = KEYS_BY_BLOCK - 1;
    }
  fkey = ss_block->keys + ss_next_in_block;
  fkey->ob_refcnt = 1;
  fkey->ob_type = &keytype;
  fkey->o = o;
  key = (PyObject*) fkey;
  n = PyDict_GetItem(ss_memo, key);
  if (n)
    {
      Py_INCREF(n);
      return n;
    }
  ss_next_in_block--;
  Py_INCREF(o);    /* reference stored in 'fkey->o' */
  t = o->ob_type;
  if (t == &PyTuple_Type)
    {
      int i, count = PyTuple_GET_SIZE(o);
      n = PyTuple_New(count);
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      for (i=0; i<count; i++)
        PyTuple_SET_ITEM(n, i, copyrec(PyTuple_GET_ITEM(o, i)));
      return n;
    }
  if (t == &PyList_Type)
    {
      int i, count = PyList_GET_SIZE(o);
      n = PyList_New(count);
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      for (i=0; i<count; i++)
        PyList_SET_ITEM(n, i, copyrec(PyList_GET_ITEM(o, i)));
      return n;
    }
  if (t == &PyDict_Type)
    {
      int i = 0;
      PyObject* dictkey;
      PyObject* dictvalue;
      n = PyDict_New();
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      while (PyDict_Next(o, &i, &dictkey, &dictvalue))
        if (PyDict_SetItem(n, copyrec(dictkey), copyrec(dictvalue)))
          goto fail;
      return n;
    }
  if (t == &PyInstance_Type)
    {
      int i = 0;
      PyObject* dictkey;
      PyObject* dictvalue;
      PyObject* dsrc;
      PyObject* ddest;
      PyObject* inst_build = PyObject_GetAttr(o, str_inst_build);
      if (inst_build == NULL)
        {
          PyErr_Clear();
          goto unmodified;
        }
      n = PyObject_CallObject(inst_build, NULL);
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      dsrc  = ((PyInstanceObject*) o)->in_dict;
      ddest = ((PyInstanceObject*) n)->in_dict;
      while (PyDict_Next(dsrc, &i, &dictkey, &dictvalue))
        if (PyDict_SetItem(ddest, copyrec(dictkey), copyrec(dictvalue)))
          goto fail;
      return n;
    }
  if (t == &PyFunction_Type)
    {
      int i, count;
      PyObject* tsrc = PyFunction_GET_DEFAULTS(o);
      PyObject* tdest;
      if (!tsrc) goto unmodified;
      count = PyTuple_GET_SIZE(tsrc);
      if (count == 0) goto unmodified;
      n = PyFunction_New(PyFunction_GET_CODE(o), PyFunction_GET_GLOBALS(o));
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      tdest = PyTuple_New(count);
      if (!tdest) goto fail;
      for (i=0; i<count; i++)
        PyTuple_SET_ITEM(tdest, i, copyrec(PyTuple_GET_ITEM(tsrc, i)));
      i = PyFunction_SetDefaults(n, tdest);
      Py_DECREF(tdest);
      if (i) goto fail;
      return n;
    }
  if (t == &PyMethod_Type)
    {
      PyObject* x;
      n = PyMethod_New(PyMethod_GET_FUNCTION(o),
                       PyMethod_GET_SELF(o),
                       PyMethod_GET_CLASS(o));
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      x = copyrec(PyMethod_GET_FUNCTION(n));
      Py_DECREF(PyMethod_GET_FUNCTION(n));
      PyMethod_GET_FUNCTION(n) = x;
      x = copyrec(PyMethod_GET_SELF(n));
      Py_DECREF(PyMethod_GET_SELF(n));
      PyMethod_GET_SELF(n) = x;
      return n;
    }
  if (t == GeneratorType)
    {
      n = genbuild(o);
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      if (gencopy(n, o)) goto fail;
      return n;
    }
  if (t == &PySeqIter_Type)
    {
      n = seqiterbuild(o);
      if (!n || PyDict_SetItem(ss_memo, key, n)) goto fail;
      if (seqitercopy(n, o)) goto fail;
      return n;
    }
  ss_next_in_block++;
  return o;     /* reference no longer stored in 'fkey->o' */

 unmodified:
  PyDict_SetItem(ss_memo, key, o);
  Py_INCREF(o);
  return o;

 fail1:
  n = NULL;
 fail:
  Py_INCREF(o);
  Py_XDECREF(n);
  return o;
}

static PyObject* sscopy(PyObject* self, PyObject* o)
{
  PyObject* n;
  ss_memo = PyDict_New();
  if (!ss_memo)
    return NULL;

  ss_block = NULL;
  ss_next_in_block = -1;
  n = copyrec(o);
  Py_DECREF(ss_memo);
  while (ss_block)
    {
      int i;
      struct key_block* b = ss_block;
      ss_block = b->next;
      for (i=ss_next_in_block+1; i<KEYS_BY_BLOCK; i++)
        Py_DECREF(b->keys[i].o);
      free(b);
      ss_next_in_block = -1;
    }
  if (PyErr_Occurred())
    {
      Py_DECREF(n);
      n = NULL;
    }
  return n;
}


static PyMethodDef StateSaverMethods[] = {
  {"copy",   sscopy,   METH_O},
  {NULL,   NULL}         /* Sentinel */
};

void initstatesaver(void)
{
  PyObject* m;
  m = Py_InitModule("statesaver", StateSaverMethods);
  if (m == NULL)
    return;
  keytype.ob_type = &PyType_Type;
  str_inst_build = PyString_InternFromString("inst_build");

  m = PyImport_ImportModule("types");
  if (!m) return;
  GeneratorType = (PyTypeObject*) PyObject_GetAttrString(m, "GeneratorType");
  if (!GeneratorType) return;
}
