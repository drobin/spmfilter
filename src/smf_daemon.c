/* spmfilter - mail filtering framework
 * Copyright (C) 2009-2010 Axel Steiner and SpaceNet AG
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <glib.h>

#include "smf_settings.h"
#include "smf_trace.h"
#include "smf_daemon.h"
#include "smf_modules.h"

#define THIS_MODULE "daemon"

static GMainLoop *event_loop;
GThreadPool *pool;
static int child_pipe[2];

void smf_daemon_term(void) {
	g_main_loop_quit(event_loop);
	g_main_loop_unref(event_loop);

	exit(0);	
}

void smf_daemon_sig_handler(int sig, siginfo_t *siginf, void *ptr) {
	switch(sig) {
		case SIGHUP:
			TRACE(TRACE_DEBUG,"GOT SIGUP");
			break;
		case SIGTERM:
			TRACE(TRACE_DEBUG,"GOT TERM");
			break;
		case SIGQUIT:
			TRACE(TRACE_DEBUG,"Received SIGNQUIT");
			smf_daemon_term();
			break;
		case SIGINT:
			TRACE(TRACE_DEBUG,"Received SIGINT");
			smf_daemon_term();
			break;
	}
	

}

int smf_daemon_drop_privileges(char *newuser, char *newgroup) {
	struct passwd *pwd = NULL;
	struct group *grp = NULL;

	grp = getgrnam(newgroup);

	if (grp == NULL) {
		TRACE(TRACE_ERR, "could not find group %s", newgroup);
		return -1;
	}

	pwd = getpwnam(newuser);
	if (pwd == NULL) {
		TRACE(TRACE_ERR, "could not find user %s", newuser);
		return -1;
	}

	if (setgid(grp->gr_gid) != 0) {
		TRACE(TRACE_ERR, "could not set gid to %s", newgroup);
		return -1;
	}

	if (setuid(pwd->pw_uid) != 0) {
		TRACE(TRACE_ERR, "could not set uid to %s", newuser);
		return -1;
	}

	return 0;
}

int smf_daemon_load_config(SMFDaemonConfig_T *config) {
	GError *error = NULL;
	GKeyFile *keyfile;
	int ip;
	SMFSettings_T *settings = smf_settings_get();

	keyfile = g_key_file_new ();
	if (!g_key_file_load_from_file (keyfile, settings->config_file, G_KEY_FILE_NONE, &error)) {
		TRACE(TRACE_ERR,"Error loading config: %s",error->message);
		g_error_free(error);
		return -1;
	}

	config->max_threads = g_key_file_get_integer(keyfile, "daemon", "max_threads",NULL);
	if (!config->max_threads) 
		config->max_threads = 15;
	TRACE(TRACE_DEBUG, "daemon->max_threads: %d",config->max_threads);


	config->timeout = g_key_file_get_integer(keyfile, "daemon", "timeout",NULL);
	if (!config->timeout) {
		config->timeout = 60;
	}
	TRACE(TRACE_DEBUG, "daemon->timeout: %d",config->timeout);

	config->port = g_key_file_get_integer(keyfile, "daemon", "bindport", NULL);
	if (!config->port) {
		config->port = 10025;
	}
	TRACE(TRACE_DEBUG, "daemon->port: %d",config->port);

	// If there was a SIGHUP, then we're resetting an active config.
	g_strfreev(config->iplist);
	g_free(config->listen_sockets);

	config->ipcount = 0;
	config->iplist = g_key_file_get_string_list(keyfile,"daemon","bindip",(gsize *)&config->ipcount,NULL);
	if (config->ipcount < 1) {
		TRACE(TRACE_ERR, "no value for bindip in config file");
	}

	for (ip = 0; ip < config->ipcount; ip++) {
		// Remove whitespace from each list entry, then log it.
		g_strstrip(config->iplist[ip]);
		TRACE(TRACE_DEBUG, "daemon->bindip: %s", config->iplist[ip]);
	}

	config->backlog = g_key_file_get_integer(keyfile, "daemon", "backlog", NULL);
	if (!config->backlog) {
		TRACE(TRACE_DEBUG, "no value for backlog in config file, using default");
		config->backlog = 16;
	}
	TRACE(TRACE_DEBUG,"daemon->backlog: %d", config->backlog);

	config->effective_user = g_key_file_get_string(keyfile, "daemon", "effective_user", NULL);
	if (config->effective_user == NULL) {
		TRACE(TRACE_ERR, "no value for EFFECTIVE_USER in config file");
		exit(-1);
	}

	TRACE(TRACE_DEBUG,"daemon->effective_user: %s", config->effective_user);

	config->effective_group = g_key_file_get_string(keyfile, "daemon", "effective_group", NULL);
	if (config->effective_group == NULL) {
		TRACE(TRACE_ERR, "no value for EFFECTIVE_GROUP in config file");
		exit(-1);
	}

	TRACE(TRACE_DEBUG,"daemon->effective_group: %s", config->effective_group);

	config->foreground = g_key_file_get_boolean(keyfile, "daemon", "foreground", NULL);
	TRACE(TRACE_DEBUG,"daemon->foreground: %d", config->foreground);

	g_key_file_free(keyfile);

	return 0;
}

pid_t smf_daemon_daemonize(void) {
	int sid;

	// double-fork
	if (fork()) exit(0);

	sid = setsid();
	chdir("/");
	umask(0077);

	if (fork()) exit(0);
	
	TRACE(TRACE_DEBUG, "daemon sid: [%d]", sid);

	return sid;
}

static int smf_daemon_create_socket(const char * const ip, int port, int backlog) {
	struct addrinfo hints, *res, *ressave;
	int sock, n, flags, err;
	int so_reuseaddress = 1;
	char *service;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype  = SOCK_STREAM;

	service = g_strdup_printf("%d",port);

	n = getaddrinfo(ip, service, &hints, &res);
	if (n < 0) {
		TRACE(TRACE_ERR, "getaddrinfo::error [%s]", gai_strerror(n));
		return -1;
	}

	ressave = res;
	if ((sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
		int serr = errno;
		freeaddrinfo(ressave);
		TRACE(TRACE_ERR, "%s", strerror(serr));
	}

	TRACE(TRACE_DEBUG, "create socket [%s:%d] backlog [%d]", ip, port, backlog);

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));

	/* bind the address */
	if ((bind(sock, res->ai_addr, res->ai_addrlen)) == -1) {
		err = errno;
		TRACE(TRACE_DEBUG, "failed");
		return err;
	}

	if ((listen(sock, backlog)) == -1) {
		err = errno;
		TRACE(TRACE_DEBUG, "failed");
		return err;
	}

	freeaddrinfo(ressave);

	// unblock
	flags = fcntl(sock, F_GETFL);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);

	return sock;
}

static gboolean child_exit(GIOChannel *io, GIOCondition cond, void *user_data) {
	int status, fd = g_io_channel_unix_get_fd(io);
	pid_t child_pid;

	if (read(fd, &child_pid, sizeof(child_pid)) != sizeof(child_pid)) {
		TRACE(TRACE_ERR, "child_exit: unable to read child pid from pipe");
		return TRUE;
	}

	if (waitpid(child_pid, &status, 0) != child_pid)
		TRACE(TRACE_ERR,"waitpid(%d) failed", child_pid);
	else
		TRACE(TRACE_DEBUG,"child %d exited", child_pid);

	return TRUE;
}

static void smf_daemon_handle(gpointer data, gpointer user_data) {
	int fd = GPOINTER_TO_INT(data);
	smf_modules_engine_load((SMFSettings_T *)user_data, fd);
	close(fd);
}

static gboolean smf_daemon_event(GIOChannel *chan, GIOCondition cond, gpointer data) {
	int i, conn, result;
	int active = 0, maxfd = 0;
	fd_set rfds;
	SMFDaemonConfig_T *config = (SMFDaemonConfig_T *)data;

	FD_ZERO(&rfds);
	for (i = 0; i < config->num_sockets; i++) {
		FD_SET(config->listen_sockets[i], &rfds);
		maxfd = MAX(maxfd, config->listen_sockets[i]);
	}

	FD_SET(child_pipe[0], &rfds);
	maxfd = MAX(maxfd, child_pipe[0]);

	result = select(maxfd + 1, &rfds, NULL, NULL, NULL);
	if (result < 1)
		return FALSE;

	if (FD_ISSET(child_pipe[0], &rfds)) {
		char buf[1];
		while (read(child_pipe[0], buf, 1) > 0)
			;
		return FALSE;
	}

	for (i = 0; i < config->num_sockets; i++) {
		if (FD_ISSET(config->listen_sockets[i], &rfds)) {
			active = i;
			break;
		}
	}

	conn = accept(config->listen_sockets[active], NULL, NULL);
	if (conn < 0)
		return FALSE;

	g_thread_pool_push(pool, GINT_TO_POINTER(conn), NULL);

	return TRUE;
}

int smf_daemon_mainloop(SMFSettings_T *settings) {
	SMFDaemonConfig_T config;
	GIOChannel *child_io;
	int i;
	struct sigaction handler;
	
	memset(&config, 0, sizeof(SMFDaemonConfig_T));
	if (smf_daemon_load_config(&config) != 0) {
		TRACE(TRACE_ERR,"failed to load daemon config");
		return -1;
	}

	memset(&handler, 0, sizeof(handler));

	handler.sa_sigaction = smf_daemon_sig_handler;
	sigemptyset(&handler.sa_mask);
	handler.sa_flags = 0;

	sigaction(SIGINT, &handler, 0);
	sigaction(SIGQUIT, &handler, 0);
	sigaction(SIGTERM, &handler, 0);
	sigaction(SIGHUP, &handler, 0);

	if (smf_daemon_drop_privileges(config.effective_user, config.effective_group) < 0) {
		TRACE(TRACE_ERR,"unable to drop privileges");
		return -1;
	}

	if (config.foreground != 1) {
		if (smf_daemon_daemonize() == -1) {
			TRACE(TRACE_DEBUG,"daemonize failed");
			return -1;
		}
	}

	config.listen_sockets = g_new0(int, config.ipcount);
	config.num_sockets = 0;

	for (i = 0; i < config.ipcount; i++) {
		config.listen_sockets[i] = smf_daemon_create_socket(config.iplist[i], config.port, config.backlog);
		config.num_sockets++;
	}

	if (pipe(child_pipe) < 0) {
		TRACE(TRACE_ERR, "pipe(): %s (%d)", strerror(errno), errno);
		exit(1);
	}

	child_io = g_io_channel_unix_new(child_pipe[0]);
	g_io_channel_set_close_on_unref(child_io, TRUE);
	g_io_add_watch(child_io,
		G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
		child_exit, NULL);
	g_io_channel_unref(child_io);

	event_loop = g_main_loop_new(NULL, FALSE);

	for (i = 0; i < config.num_sockets; i++) {
		GIOChannel *ctl_io;
		ctl_io = g_io_channel_unix_new(config.listen_sockets[i]);
		g_io_channel_set_close_on_unref(ctl_io, TRUE);
		g_io_channel_set_encoding(ctl_io, NULL, NULL);
		g_io_add_watch(ctl_io, G_IO_IN, smf_daemon_event, &config);
		g_io_channel_unref(ctl_io);
	}

	g_thread_init(NULL);
	
	pool = g_thread_pool_new((GFunc) smf_daemon_handle,settings,config.max_threads,FALSE,NULL);
	g_main_loop_run(event_loop);

	g_main_loop_unref(event_loop);
	
	return 0;
}