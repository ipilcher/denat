#
# Copyright 2017, 2019 Ian Pilcher <arequipeno@gmail.com>
#
# This program is free software.  You can redistribute it or modify it under
# the terms of version 2 of the GNU General Public License (GPL), as published
# by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY -- without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the text of the GPL for more details.
#
# Version 2 of the GNU General Public License is available at:
#
#	http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
#

import netaddr
import re
import textwrap

#
# When wrapping comment lines, list items require special treatment.  Lines
# after the first item in the item (if any) must be indented to line up with
# the item text.
#
# This regex matches list item "bullets" of the folliwng forms:
#
#	  *  A single asterisk
#	  -  A single hypher
#	 a.  A single letter (upper- or lowercase), followed by a period
#	 B)  A single letter, followed by a right parenthesis
#	 1.  One or more digits, followed by a period
#	15)  One of more digits, followed by a right parenthesis
#
# Bullets that begin with a left parenthesis, such as (a) or (1), are not
# supported.
#
# Regex components:
#
#	[ \t]*			Optional whitespace (spaces & tabs)
#	(?:			Begin "non-capturing" parentheses
#		-		A single hyphen
#		\*		A single asterisk
#		[A-Za-z][.)]	A single letter, followed by a period or
#				right parenthesis
#		[0-9]+[.)]	One or more digits, followed by a period or
#				right parenthesis
#	)			End non-capturing parentheses
#	[ \t]+			Required whitespace
#
# The regex is used to identify list item lines and to determine the
# indentation of the second and subsequent lines of a wrapped list item.
#
_ITEM_REGEX = re.compile(r'([ \t]*(?:-|\*|[A-Za-z][.)]|[0-9]+[.)])[ \t]+)')

# Regex which matches non-whitespace in list item "bullets"
_BULLET_REGEX = re.compile(r'[-*A-Za-z0-9.)]')

# Regex used to identify/process trailing whitespace
_TRAILING_WS_REGEX = re.compile(r'\s+$')

# Regex used to identify/process leading whitespace
_LEADING_WS_REGEX = re.compile(r'(\s+)(.*)')

_COMMENT_WRAPPER = textwrap.TextWrapper(width=80, expand_tabs=False,
					replace_whitespace=False)


def _fix_end(s):
	"""
	"Fix" the end of a comment input string.

	Returns a (possibly) modified copy of s that is suitable for further
	processing. The returned string is constructed as follows:

	1. Strip any whitespace characters from the end of s.

	2. If the removed whitespace (if any) included any newline characters,
	   add that number of newlines at the end of s.

	3. If s does not end with a newline, add a single space at its end.
	"""

	match = _TRAILING_WS_REGEX.search(s)
	if match:
		s = s[:match.start()]
		newlines = match.group().count('\n')
		if newlines:
			return s + ('\n' * newlines)

	if s:
		return s + ' '
	else:
		return s


def _fix_start(s):
	"""
	"Fix" the beginning of a comment input string.

	Returns a 2-item tuple.

	* The first item is a (possibly) modified copy of s that is suitable for
	  use as the continuation of a paragraph.  I.e. any whitespace
	  characters are stripped from the begginning of s, except that newline
	  characters are preserved (as by _fix_ends).

	* The second item is the string of whitespace that was removed from s to
	  create the first item.  (It will be an empty string if nothing was
	  removed.)  This item is used to check subsequent input lines for
	  identitical indentation.

	_fix_start(s)[1] + _fix_start(s)[0] == s
	"""

	match = _LEADING_WS_REGEX.match(s)
	if match:
		newlines = match.group(1).count('\n')
		if newlines:
			return ('\n' * newlines) + match.group(2), match.group(1)
		else:
			return match.group(2), match.group(1)

	return s, ''


def _mkparas(comment):
	"""
	Create a list of comment "paragraphs" from 1 or more input strings.

	comment may be a single string of a list of strings.  The string(s)
	will be modified and combined as appropriate to create a list of
	paragraphs.  Each paragraph is suitable for output with a TextWrapper.
	"""

	if not isinstance(comment, list):
		comment = [ comment ]

	paras = []	# List of "paragraphs"
	para = ''	# Paragraph being built, possibly from >1 input lines
	citem = False	# Are we continuing a list item?
	cstart = None	# Regex used to check for matching indentation

	for s in comment:

		s = _fix_end(s)

		if not s:
			if para:
				paras += para.split('\n')
			paras.append('')
			para = ''
			cstart = None
			citem = False

		item_match = _ITEM_REGEX.match(s)
		if item_match:
			if para:
				paras += para.split('\n')
			para = s
			citem = True
			continue

		if citem or (cstart and cstart.match(s)):
			para += _fix_start(s)[0]
			continue

		if para:
			paras += para.split('\n')
		para = s
		cstart = re.compile(_fix_start(s)[1] + r'\S')

	if para:
		paras += para.split('\n')

	return paras


def _write_comment(fh, comment, prefix):
	"""
	Write a "re-flowed" comment to fh.

	Given a list of "input strings" (or a single string) in comment,
	write a re-flowed comment to fh, where each output line begins with
	prefix.
	"""

	_COMMENT_WRAPPER.initial_indent = prefix

	paras = _mkparas(comment)

	for p in paras:
		match = _ITEM_REGEX.match(p)
		if match:
			_COMMENT_WRAPPER.subsequent_indent = (
				prefix + _BULLET_REGEX.sub(' ', match.group(1)))
		else:
			match = _LEADING_WS_REGEX.match(p)
			if match:
				_COMMENT_WRAPPER.subsequent_indent = (
					prefix + match.group(1))
			else:
				_COMMENT_WRAPPER.subsequent_indent = prefix

		fh.write(_COMMENT_WRAPPER.fill(p) if p else prefix)
		fh.write('\n')


def _fmt(o):
	"""
	"""

	if isinstance(o, bool):
		if o:
			return 'on'
		else:
			return 'off'
	else:
		return o


def _write_interface_options(fh, opts):
	"""
	"""

	if '__COMMENT__' in opts:
		fh.write('\n')
		_write_comment(fh, opts['__COMMENT__'], '\t# ')

	fh.write('\n')

	for key, value in opts.iteritems():
		if key == '__COMMENT__':
			continue
		fh.write('\t%s %s;\n' % (key, _fmt(value)))


def _write_stanza_options(fh, opts):
	"""
	"""

	for key, value in opts.iteritems():
		if key in [ '__TYPE__', '__COMMENT__', '__DEPRECATED__' ]:
			continue
		fh.write('\t\t%s %s;\n' % (key, _fmt(value)))


def _write_stanza(fh, name, cfg, new, old):
	"""
	"""

	stanza_type = cfg['__TYPE__']

	if stanza_type == 'dynamic_prefix':
		if old and '__DEPRECATED__' in cfg:
			cfg['__DEPRECATED__']['__TYPE__'] = 'dynamic_prefix'
			_write_stanza(fh, name, cfg['__DEPRECATED__'], old, None)
		prefix = netaddr.IPNetwork(name)
		name = str(new.ip | prefix.ip) + '/' + str(prefix.prefixlen)
		stanza_type = 'prefix'

	if stanza_type == 'static_prefix':
		stanza_type = 'prefix'

	if '__COMMENT__' in cfg:
		fh.write('\n')
		_write_comment(fh, cfg['__COMMENT__'], '\t# ')
		#fh.write('\n')

	fh.write('\n\t%s %s\n\t{\n' % (stanza_type, name))
	_write_stanza_options(fh, cfg)
	fh.write('\t};\n')


def _write_interface(fh, name, cfg, new, old):
	"""
	"""

	if '__COMMENT__' in cfg:
		fh.write('\n')
		_write_comment(fh, cfg['__COMMENT__'], '# ')

	fh.write('\ninterface %s\n{' % name)

	if '__OPTIONS__' in cfg:
		_write_interface_options(fh, cfg['__OPTIONS__'])

	for key, value in cfg.iteritems():
		if key == '__COMMENT__' or key == '__OPTIONS__':
			continue
		_write_stanza(fh, key, value, new, old)

	fh.write('};\n')


def write_conf(fh, cfg, new, old):
	"""
	"""

	# When denatc first starts, we may be called with old == new

	if old == new:
		old = None

	if '__COMMENT__' in cfg:
		#print repr(cfg['__COMMENT__'])
		fh.write('\n###\n')
		_write_comment(fh, cfg['__COMMENT__'], '###\t')
		fh.write('###\n')

	for key, value in cfg.iteritems():
		if key == '__COMMENT__':
			continue
		_write_interface(fh, key, value, new, old)
