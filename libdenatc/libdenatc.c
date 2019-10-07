#define _DEFAULT_SOURCE			/* for setgroups(2) */

#include <Python.h>

/*
 * Verify that Python.h defined _GNU_SOURCE, which is required for setresuid(2)
 * and setresgid(2).  Defining it before including Python.h will cause a
 * diagnostic:
 *
 * 	warning: "_GNU_SOURCE" redefined
 *
 * So we rely on Python.h to define it, but verify that it actually did so.
 */
#ifndef _GNU_SOURCE			/* setresuid(2) & setresgid(2) */
#error	Python.h did not define _GNU_SOURCE
#endif

#include <grp.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/types.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * Verify that we've got the GNU version of strerror_r(3)
 */
__attribute__((unused))
static inline void check_for_gnu_strerror_r(void)
{
	/*
	 * If this causes an 'invalid type argument of unary `*`' error,
	 * strerror_r is the XSI-compliant version; we need the GNU version.
	 * See strerror_r(3).
	 */
	if (0) {
		char c = *strerror_r(0, &c, 0);		/* see above */
	}
}

/*
 * The "skeleton" of the module
 */

static const char module_name[] = "libdenatc";

/* An exception class for libcap errors */
static PyObject *ce_class;
static char ce_docstr[] =
		"Linux capability-related errors (returned by libcap)";

/* The libdenatc.drop_root method */
static PyObject *libdenatc_drop_root(PyObject *self, PyObject *args);

/* Module method table */
static PyMethodDef methods[] = {
	{ "drop_root", libdenatc_drop_root, METH_VARARGS, NULL },
	{ NULL, NULL, 0, NULL }
};

/* Module init function */
PyMODINIT_FUNC initlibdenatc(void)
{
	PyObject *module;

	module = Py_InitModule(module_name, methods);

	ce_class = PyErr_NewExceptionWithDoc("libdenatc.CapabilityError",
					     ce_docstr, PyExc_EnvironmentError,
					     NULL);
	Py_INCREF(ce_class);
	PyModule_AddObject(module, "CapabilityError", ce_class);
}

/*
 * Set an exception (a subclass of EnvironmentError) with an errno value
 * and a custom message.
 */
static PyObject *mkerr(const PyObject *const class, const char *const format,
		       cap_t state)
{
	PyObject *err_args, *err_str;
	char buf[100];
	int errnum;

	errnum = errno;

	err_str = PyString_FromFormat(format,
				      strerror_r(errnum, buf, sizeof buf));
	if (err_str == NULL)
		return NULL;

	if ((err_args = PyTuple_New(2)) == NULL) {
		Py_DECREF(err_str);
		return NULL;
	}

	PyTuple_SetItem(err_args, 0, PyInt_FromLong(errnum));
	PyTuple_SetItem(err_args, 1, err_str);
	PyErr_SetObject(ce_class, err_args);

	if (state != NULL)
		cap_free(state);

	return NULL;
}

static PyObject *drop_root(const uid_t uid, const gid_t gid)
{
	static const cap_value_t caps[4] = {
		CAP_NET_ADMIN, CAP_SETUID, CAP_SETGID, CAP_SETPCAP
	};

	cap_t state;

	if ((state = cap_get_proc()) == NULL)
		return mkerr(ce_class, "cap_get_proc: %s", NULL);

	if (cap_clear(state) != 0)
		return mkerr(ce_class, "Pre-drop: cap_clear: %s", state);

	if (cap_set_flag(state, CAP_EFFECTIVE, 4, caps, CAP_SET) != 0) {
		return mkerr(ce_class,
			     "Pre-drop: cap_set_flag(CAP_EFFECTIVE): %s",
			     state);
	}

	if (cap_set_flag(state, CAP_PERMITTED, 4, caps, CAP_SET) != 0) {
		return mkerr(ce_class,
			     "Pre-drop: cap_set_flag(CAP_PERMITTED): %s",
			     state);
	}

	if (cap_set_proc(state) != 0)
		return mkerr(ce_class, "Pre-drop: cap_set_proc: %s", state);

	if (prctl(PR_SET_KEEPCAPS, 1L) != 0)
		return mkerr(PyExc_OSError, "Pre-drop: prctl: %s", state);

	if (setgroups(1, &gid) != 0)
		return mkerr(PyExc_OSError, "setgroups: %s", state);

	if (setresgid(gid, gid, gid) != 0)
		return mkerr(PyExc_OSError, "setresgid: %s", state);

	if (setresuid(uid, uid, uid) != 0)
		return mkerr(PyExc_OSError, "setresuid: %s", state);

	if (cap_clear(state) != 0)
		return mkerr(ce_class, "Post-drop: cap_clear: %s", state);

	if (cap_set_flag(state, CAP_PERMITTED, 1, caps, CAP_SET) != 0) {
		return mkerr(ce_class,
			     "Post-drop: cap_set_flag(CAP_PERMITTED): %s",
			     state);
	}

	if (cap_set_flag(state, CAP_EFFECTIVE, 1, caps, CAP_SET) != 0) {
		return mkerr(ce_class,
			     "Post-drop: cap_set_flag(CAP_EFFECTIVE): %s",
			     state);
	}

	if (cap_set_proc(state) != 0)
		return mkerr(ce_class, "Post-drop: cap_set_proc: %s", state);

	if (cap_free(state) != 0)
		return mkerr(ce_class, "cap_free: %s", NULL);

	if (prctl(PR_SET_KEEPCAPS, 0L) != 0)
		return mkerr(ce_class, "Post-drop: prctl: %s", NULL);

	Py_RETURN_NONE;
}

static PyObject *libdenatc_drop_root(PyObject *const self
						__attribute__((unused)),
				     PyObject *const args)
{
	uid_t uid;
	gid_t gid;

	if (PyArg_ParseTuple(args, "II", &uid, &gid) == 0)
		return NULL;

	if (uid == 0) {
		PyErr_SetString(PyExc_ValueError, "UID must be non-zero");
		return NULL;
	}

	if (gid == 0) {
		PyErr_SetString(PyExc_ValueError, "GID must be non-zero");
		return NULL;
	}

	return drop_root(uid, gid);
}

