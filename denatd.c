#define _BSD_SOURCE		/* for vsyslog */

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>

#include <inttypes.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#define EXEC_NAME	"denatd"
#define OUTBUF_SIZE	1000

static const char prefix_file[] = "/run/dhclient6-prefix";

/*
 *      Command-line options
 */

/* Log to stderr instead of syslog? */
static _Bool debug = 0;

/* Log debug-level messages? */
static _Bool verbose = 0;

/* Listen port (host byte order) */
static uint16_t lport = 9797;

static sa_family_t ip_version = AF_UNSPEC;

/* INADDR_ANY is 0x00000000, so byte order doesn't matter */
static struct in_addr laddr4 = { .s_addr = INADDR_ANY };

static struct in6_addr laddr6 = IN6ADDR_ANY_INIT;

/*
 *      Logging
 */

static void vlog(int priority, const char *fmt, va_list ap)
{
        if (debug) {
                fputs(EXEC_NAME ": ", stderr);
                vfprintf(stderr, fmt, ap);
        }
        else {
                vsyslog(priority, fmt, ap);
        }
}

__attribute__((format(printf, 1, 2)))
static void dbug(const char *fmt, ...)
{
        va_list ap;

        if (verbose) {
                va_start(ap, fmt);
                vlog(LOG_INFO, fmt, ap);
                va_end(ap);
        }
}

__attribute__((format(printf, 1, 2)))
static void error(const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        vlog(LOG_ERR, fmt, ap);
        va_end(ap);
}

__attribute__((format(printf, 1, 2)))
static void warn(const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        vlog(LOG_WARNING, fmt, ap);
        va_end(ap);
}

__attribute__((format(printf, 1, 2)))
static void info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlog(LOG_INFO, fmt, ap);
	va_end(ap);
}

/*
 *	Option parsing
 */

__attribute__((noreturn))
static void show_help(int status)
{
	printf("Usage: %s [-4|--ipv4] [-d|--debug] [-v|--verbose] [-h|--help]\n"
	       "\t[-l|--listen address] [-p|--port port]\n", EXEC_NAME);
	exit(status);
}

static void ip_version_mismatch(void)
{
	char buf[INET6_ADDRSTRLEN];

	if (inet_ntop(AF_INET6, &laddr6, buf, sizeof buf) == NULL) {
		perror("inet_ntop");
		abort();
	}

	fprintf(stderr, "%s: IPv6 listen address (%s) not compatible "
		"with IPv4 option (-4|--ipv4)\n", EXEC_NAME, buf);
	show_help(EXIT_FAILURE);
}

static int parse_debug(int i __attribute__((unused)),
		       int argc __attribute__((unused)),
		       char *argv[] __attribute__((unused)))
{
	debug = 1;
	return 0;
}

static int parse_verbose(int i __attribute__((unused)),
			 int argc __attribute__((unused)),
			 char *argv[] __attribute__((unused)))
{
	verbose = 1;
	return 0;
}

static int parse_ipv4(int i __attribute__((unused)),
		      int argc __attribute__((unused)),
		      char *argv[] __attribute__((unused)))
{
	if (ip_version == AF_INET6)
		ip_version_mismatch();

	ip_version = AF_INET;
	return 0;
}

static int parse_help(int i __attribute((unused)),
		      int argc __attribute((unused)),
		      char *argv[] __attribute__((unused)))
{
	show_help(EXIT_SUCCESS);
}

static int parse_lport(int i, int argc, char *argv[])
{
	char *endptr;
	long port;

	if (++i >= argc) {
		fprintf(stderr, "%s: %s option requires an argument\n",
			EXEC_NAME, argv[i - 1]);
		show_help(EXIT_FAILURE);
	}

	if (isspace(*argv[i]) || *argv[i] == 0)
		goto invalid_port;

	errno = 0;
	port = strtol(argv[i], &endptr, 0);
	if (errno != 0 || *endptr != 0 || port < 0 || port > UINT16_MAX)
		goto invalid_port;

	lport = (uint16_t)port;

	return 1;

invalid_port:
	fprintf(stderr, "%s: invalid argument for %s option: '%s'\n",
		EXEC_NAME, argv[i - 1], argv[i]);
	show_help(EXIT_FAILURE);
}

static int parse_laddr(int i, int argc, char *argv[])
{
	if (++i >= argc) {
		fprintf(stderr, "%s: %s option requires an argument\n",
			EXEC_NAME, argv[i - 1]);
		show_help(EXIT_FAILURE);
	}

	if (inet_pton(AF_INET6, argv[i], &laddr6) == 1) {
		if (ip_version == AF_INET)
			ip_version_mismatch();
		ip_version = AF_INET6;
	}
	else if (inet_pton(AF_INET, argv[i], &laddr4) == 1) {
		ip_version = AF_INET;
	}
	else {
		fprintf(stderr, "%s: invalid argument for %s option: '%s'\n",
			EXEC_NAME, argv[i - 1], argv[i]);
		show_help(EXIT_FAILURE);
	}

	return 1;
}

struct option {
	const char *short_opt;
	const char *long_opt;
	int (*parse_fn)(int i, int argc, char *argv[]);
	_Bool called;
};

static struct option  options[] = {
	{ "-4",	"--ipv4", 	parse_ipv4, 	0 },
	{ "-d", "--debug", 	parse_debug, 	0 },
	{ "-v", "--verbose",	parse_verbose,	0 },
	{ "-p", "--port", 	parse_lport, 	0 },
	{ "-l", "--listen", 	parse_laddr, 	0 },
	{ "-h", "--help", 	parse_help, 	0 },
	{ NULL, NULL, 		0, 		0 }
};
	
/* Errors during argument parsing are sent to stderr; systemd should log them */
static void parse_args(int argc, char *argv[])
{
	char buf[INET6_ADDRSTRLEN];
	struct option *o;
	int i;

	for (i = 1; i < argc; ++i) {

		for (o = options; o->short_opt != NULL; ++o) {

			if (strcmp(argv[i], o->short_opt) == 0 ||
					strcmp(argv[i], o->long_opt) == 0) {

				if (!o->called) {
					i += o->parse_fn(i, argc, argv);
					o->called = 1;
					break;
				}

				fprintf(stderr,
					"%s: multiple %s or %s options\n",
					EXEC_NAME, o->short_opt, o->long_opt);
				show_help(EXIT_FAILURE);
			}
		}

		if (o->called)
			continue;

		fprintf(stderr, "%s: invalid option: '%s'\n", EXEC_NAME,
			argv[i]);
		show_help(EXIT_FAILURE);
	}

	if (ip_version == AF_UNSPEC)
		ip_version = AF_INET6;

	if (verbose) {
        	dbug("debug = %d\n", debug);
	        dbug("verbose = %d\n", verbose);
        	dbug("lport = %" PRIu16 "\n", lport);
	        dbug("ip_version = %d\n", ip_version);
        	dbug("laddr4 = %s\n",
		     inet_ntop(AF_INET, &laddr4, buf, sizeof buf));
	        dbug("laddr6 = %s\n",
		     inet_ntop(AF_INET6, &laddr6, buf, sizeof buf));
	}
}

/*
 *	Output buffer
 */

static char outbuf[OUTBUF_SIZE];
static int cursor = 0;

__attribute__((format(printf, 1, 2)))
static int bprintf(const char *fmt, ...)
{
	va_list ap;
	int ret;

	if (cursor >= (int)(sizeof outbuf) - 1)
		return 1;

	va_start(ap, fmt);
	ret = vsnprintf(outbuf + cursor, sizeof outbuf - cursor, fmt, ap);
	va_end(ap);

	if (ret < 0) {
		error("vsnprintf: %m\n");
		abort();
	}

	if (ret >= (int)(sizeof outbuf) - cursor - 1) {
		cursor = sizeof outbuf - 1;
		return 1;
	}
	else {
		cursor += ret;
		return 0;
	}
}

/*
 *	Main loop
 */

union sockaddr_inX {
	struct sockaddr a;
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
};

static void get_ips(void)
{
	char addrbuf[INET6_ADDRSTRLEN];
	struct ifaddrs *ifa, *ifaddrs;
	sa_family_t family;
	void *inX_addr;
	int truncated;

        if (getifaddrs(&ifaddrs) < 0) {
                error("getifaddrs: %m\n");
                exit(EXIT_FAILURE);
        }

        truncated = 0;

        for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {

                if (ifa->ifa_addr == NULL) {
                        warn("NULL address on interface %s\n",
                             ifa->ifa_name);
                        continue;
                }

                family = ifa->ifa_addr->sa_family;

                if (family == AF_INET) {
                        inX_addr = &((struct sockaddr_in *)
                                                ifa->ifa_addr)->sin_addr;
                }
                else if (family == AF_INET6) {
                        inX_addr = &((struct sockaddr_in6 *)
                                                ifa->ifa_addr)->sin6_addr;
                }
                else if (family == AF_PACKET) {
                        continue;
                }
                else {
                        warn("Unknown address family (%u) on interface %s\n",
                             family, ifa->ifa_name);
                        continue;
                }

                if (inet_ntop(family, inX_addr, addrbuf,
                                                sizeof addrbuf) == NULL) {
                        error("inet_ntop: %m\n");
                        abort();
                }

                truncated = bprintf("%s %s\n", ifa->ifa_name, addrbuf);
        }

	freeifaddrs(ifaddrs);

        if (truncated)
                warn("Output truncated\n");
}

static void get_prefix(void)
{
	char buf[100];
	ssize_t size;
	int fd;
	
	if ((fd = open(prefix_file, O_RDONLY)) < 0) {
		if (errno == ENOENT) {
			info("Prefix file (%s) missing\n", prefix_file);
			return;
		}
		error("%s: %m\n", prefix_file);
		abort();
	}
	
	if ((size = read(fd, buf, sizeof buf)) < 0) {
		error("%s: %m\n", prefix_file);
		abort();
	}
	
	if (size > 0) {
		--size;
		if (buf[size] != '\n') {
			warn("Prefix file (%s) not newline terminated\n",
			     prefix_file);
		}
		else {
			buf[size] = '\0';
			if (bprintf("__PREFIX__ %s\n", buf))
				warn("Output truncated\n");
		}
	}
	
	if (close(fd) < 0) {
		error("%s: %m\n", prefix_file);
		abort();
	}
}

static int get_socket(void)
{
	char buf[INET6_ADDRSTRLEN];
	union sockaddr_inX addr;
	socklen_t addrlen;
	int fd;

	fd = socket(ip_version, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		error("socket: %m\n");
		abort();
	}

	if (ip_version == AF_INET) {

		addr.in.sin_family = AF_INET;
		addr.in.sin_port = htons(lport);
		addr.in.sin_addr = laddr4;
		addrlen = sizeof addr.in;

		if (inet_ntop(AF_INET, &laddr4, buf, sizeof buf) == NULL) {
			error("inet_ntop: %m\n");
			abort();
		}
	}
	else {
		addr.in6.sin6_family = AF_INET6;
		addr.in6.sin6_port = htons(lport);
		addr.in6.sin6_addr = laddr6;
		addrlen = sizeof addr.in6;

		if (inet_ntop(AF_INET6, &laddr6, buf, sizeof buf) == NULL) {
			error("inet_ntop: %m\n");
			abort();
		}
	}

	if (bind(fd, &addr.a, addrlen) < 0) {
		error("bind: %m\n");
		abort();
	}

	if (listen(fd, 1) < 0) {
		error("listen: %m\n");
		abort();
	}

	info("Listening on %s/%" PRIu16 "\n", buf, lport);

	return fd;
}

static void log_conn(union sockaddr_inX *addr)
{
	char buf[INET6_ADDRSTRLEN];
	uint16_t port;
	void *src;

	if (addr->a.sa_family == AF_INET) {
		src = &addr->in.sin_addr;
		port = ntohs(addr->in.sin_port);
	}
	else {
		src = &addr->in6.sin6_addr;
		port = ntohs(addr->in6.sin6_port);
	}

	if (inet_ntop(addr->a.sa_family, src, buf, sizeof buf) == NULL) {
		error("inet_ntop: %m\n");
		abort();
	}

	dbug("Connection from %s/%" PRIu16 "\n", buf, port);
}
		

int main(int argc, char *argv[])
{
	union sockaddr_inX sockaddr;
	int listen_fd, sockfd;
	socklen_t addrlen;

	parse_args(argc, argv);
	if (!debug)
		openlog(EXEC_NAME, LOG_PID, LOG_USER);

	listen_fd = get_socket();

	while (1) {

		cursor = 0;

		addrlen = sizeof sockaddr;
		sockfd = accept(listen_fd, &sockaddr.a, &addrlen);
		if (sockfd < 0) {
			error("accept: %m\n");
			abort();
		}

		if (verbose)
			log_conn(&sockaddr);

		get_ips();
		get_prefix();

		if (write(sockfd, outbuf, cursor) != cursor)
			warn("write: %m\n");

		if (close(sockfd) < 0) {
			error("close: %m\n");
			abort();
		}

		dbug("Connection closed\n");
	}
}
