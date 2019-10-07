from distutils.core import setup, Extension

long_desc = '''
Python wrappers for C functions used by denatc.  Currently, this
library contains a single function, libdenatc.drop_root, which
drops root privileges (by changing to the UID and GID of a non-
root user) while retaining the CAP_NET_ADMIN capability.
'''

extension = Extension('libdenatc',
		      sources = [ 'libdenatc.c' ],
		      libraries = [ 'cap' ]
);

setup(name = 'libdenatc',
      version = '0.1',
      description = 'C function wrappers for denatc',
      author = 'Ian Pilcher',
      author_email = 'arequipeno@gmail.com',
      url = 'https://github.com/ipilcher/denat',
      long_description = long_desc,
      ext_modules = [ extension ]
)
