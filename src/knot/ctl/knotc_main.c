/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <getopt.h>

#include "knot/common.h"
#include "knot/other/error.h"
#include "knot/ctl/process.h"
#include "knot/conf/conf.h"
#include "knot/zone/zone-load.h"

/*! \brief Controller constants. */
enum knotc_constants_t {
	WAITPID_TIMEOUT = 10 /*!< \brief Timeout for waiting for process. */
};

/*! \brief Controller flags. */
enum knotc_flag_t {
	F_NULL        = 0 << 0,
	F_FORCE       = 1 << 0,
	F_VERBOSE     = 1 << 1,
	F_WAIT        = 1 << 2,
	F_INTERACTIVE = 1 << 3,
	F_AUTO        = 1 << 4
};

static inline unsigned has_flag(unsigned flags, enum knotc_flag_t f) {
	return flags & f;
}

/*! \brief Print help. */
void help(int argc, char **argv)
{
	printf("Usage: %sc [parameters] start|stop|restart|reload|running|"
	       "compile\n", PACKAGE_NAME);
	printf("Parameters:\n"
	       " -c [file], --config=[file] Select configuration file.\n"
	       " -j [num], --jobs=[num]     Number of parallel tasks to run (only for 'compile').\n"
	       " -f, --force                Force operation - override some checks.\n"
	       " -v, --verbose              Verbose mode - additional runtime information.\n"
	       " -V, --version              Print %s server version.\n"
	       " -w, --wait                 Wait for the server to finish start/stop operations.\n"
	       " -i, --interactive          Interactive mode (do not daemonize).\n"
	       " -a, --auto                 Enable automatic recompilation (start or reload)."
	       " -h, --help                 Print help and usage.\n",
	       PACKAGE_NAME);
	printf("Actions:\n"
	       " start     Start %s server zone (no-op if running).\n"
	       " stop      Stop %s server (no-op if not running).\n"
	       " restart   Stops and then starts %s server.\n"
	       " reload    Reload %s configuration and compiled zones.\n"
	       " running   Check if server is running.\n"
	       " checkconf Check server configuration.\n"
	       "\n"
	       " compile   Compile zone file.\n",
	       PACKAGE_NAME, PACKAGE_NAME, PACKAGE_NAME, PACKAGE_NAME);
}

/*!
 * \brief Check if the zone needs recompilation.
 *
 * \param db Path to zone db file.
 * \param source Path to zone source file.
 *
 * \retval KNOTD_EOK if up to date.
 * \retval KNOTD_ERROR if needs recompilation.
 */
int check_zone(const char *db, const char* source)
{
	/* Check zonefile. */
	struct stat st;
	if (stat(source, &st) != 0) {
		int reason = errno;
		const char *emsg = "";
		switch(reason) {
		case EACCES:
			emsg = "Not enough permissions to access zone file '%s'.\n";
			break;
		case ENOENT:
			emsg = "Zone file '%s' doesn't exist.\n";
			break;
		default:
			emsg = "Unable to stat zone file '%s'.\n";
			break;
		}
		
		fprintf(stderr, "error: ");
		fprintf(stderr, emsg, source);
		return KNOTD_ENOENT;
	}

	/* Read zonedb header. */
	zloader_t *zl = 0;
	knot_zload_open(&zl, db);
	if (!zl) {
		return KNOTD_ERROR;
	}

	/* Check source files and mtime. */
	int ret = KNOTD_ERROR;
	int src_changed = strcmp(source, zl->source) != 0;
	if (!src_changed && !knot_zload_needs_update(zl)) {
		ret = KNOTD_EOK;
	}

	knot_zload_close(zl);
	return ret;
}

pid_t wait_cmd(pid_t proc, int *rc)
{
	/* Wait for finish. */
	sigset_t newset;
	sigfillset(&newset);
	sigprocmask(SIG_BLOCK, &newset, 0);
	proc = waitpid(proc, rc, 0);
	sigprocmask(SIG_UNBLOCK, &newset, 0);
	return proc;
}

pid_t start_cmd(const char *argv[], int argc)
{
	pid_t chproc = fork();
	if (chproc == 0) {

		/* Duplicate, it doesn't run from stack address anyway. */
		char **args = malloc((argc + 1) * sizeof(char*));
		memset(args, 0, (argc + 1) * sizeof(char*));
		int ci = 0;
		for (int i = 0; i < argc; ++i) {
			if (strlen(argv[i]) > 0) {
				args[ci++] = strdup(argv[i]);
			}
		}
		args[ci] = 0;

		/* Execute command. */
		fflush(stdout);
		fflush(stderr);
		execvp(args[0], args);

		/* Execute failed. */
		fprintf(stderr, "Failed to run executable '%s'\n", args[0]);
		for (int i = 0; i < argc; ++i) {
			free(args[i]);
		}
		free(args);

		exit(1);
		return -1;
	}
	
	return chproc;
}

int exec_cmd(const char *argv[], int argc)
{
	int ret = 0;
	pid_t proc = start_cmd(argv, argc);
	wait_cmd(proc, &ret);
	return ret;
}

/*! \brief Zone compiler task. */
typedef struct {
	conf_zone_t *zone;
	pid_t proc;
} knotc_zctask_t;

/*! \brief Create set of watched tasks. */
knotc_zctask_t *zctask_create(int count)
{
	if (count <= 0) {
		return 0;
	}
	
	knotc_zctask_t *t = malloc(count * sizeof(knotc_zctask_t));
	for (unsigned i = 0; i < count; ++i) {
		t[i].proc = -1;
		t[i].zone = 0;
	}
	
	return t;
}

/*! \brief Wait for single task to finish. */
int zctask_wait(knotc_zctask_t *tasks, int count)
{
	/* Wait for children to finish. */
	int rc = 0;
	pid_t pid = wait_cmd(-1, &rc);
	
	/* Find task. */
	conf_zone_t *z = 0;
	for (unsigned i = 0; i < count; ++i) {
		if (tasks[i].proc == pid) {
			tasks[i].proc = -1; /* Invalidate. */
			z = tasks[i].zone;
			break;
		}
	}
	
	if (z == 0) {
		fprintf(stderr, "error: Failed to find zone for finished "
		        "zone compilation process.\n");
		return 1;
	}
	
	/* Evaluate. */
	if (!WIFEXITED(rc)) {
		fprintf(stderr, "error: Compilation of '%s' "
		        "failed, process was killed.\n",
		        z->name);
		return 1;
	} else {
		if (rc < 0 || WEXITSTATUS(rc) != 0) {
			fprintf(stderr, "error: Compilation of "
			        "'%s' failed, knot-zcompile "
			        "return code was '%d'\n",
			        z->name, WEXITSTATUS(rc));
			return 1;
		}
	}
	
	return 0;
}

/*! \brief Register running zone compilation process. */
int zctask_add(knotc_zctask_t *tasks, int count, pid_t pid, conf_zone_t *zone)
{
	/* Find free space. */
	for (unsigned i = 0; i < count; ++i) {
		if (tasks[i].proc == -1) {
			tasks[i].proc = pid;
			tasks[i].zone = zone;
			return 0;
		}
	}
	
	/* Free space not found. */
	return -1;
}

/*!
 * \brief Execute specified action.
 *
 * \param action Action to be executed (start, stop, restart...)
 * \param argv Additional arguments vector.
 * \param argc Addition arguments count.
 * \param pid Specified PID for action.
 * \param verbose True if running in verbose mode.
 * \param force True if forced operation is required.
 * \param wait Wait for the operation to finish.
 * \param interactive Interactive mode.
 * \param automatic Automatic compilation.
 * \param jobs Number of parallel tasks to run.
 * \param pidfile Specified PID file for action.
 *
 * \retval 0 on success.
 * \retval error return code for main on error.
 */
int execute(const char *action, char **argv, int argc, pid_t pid,
            unsigned flags, unsigned jobs, const char *pidfile)
{
	int valid_cmd = 0;
	int rc = 0;
	if (strcmp(action, "start") == 0) {
		// Check pidfile for w+
		FILE* chkf = fopen(pidfile, "w+");
		if (chkf == NULL) {
			fprintf(stderr, "control: PID file '%s' is not writeable, refusing to start\n", pidfile);
			return 1;
		} else {
			fclose(chkf);
			chkf = NULL;
		}

		// Check PID
		valid_cmd = 1;
//		if (pid < 0 && pid == KNOT_ERANGE) {
//			fprintf(stderr, "control: Another server instance "
//					 "is already starting.\n");
//			return 1;
//		}
		if (pid > 0 && pid_running(pid)) {

			fprintf(stderr, "control: Server PID found, "
			        "already running.\n");

			if (!has_flag(flags, F_FORCE)) {
				return 1;
			} else {
				fprintf(stderr, "control: forcing "
					"server start, killing old pid=%ld.\n",
					(long)pid);
				kill(pid, SIGKILL);
				pid_remove(pidfile);
			}
		}

		// Recompile zones if needed
		if (has_flag(flags, F_AUTO)) {
			rc = execute("compile", argv, argc, -1, flags,
			             jobs, pidfile);
		}

		// Lock configuration
		conf_read_lock();

		// Prepare command
		const char *cfg = conf()->filename;
		const char *args[] = {
			PROJECT_EXEC,
			has_flag(flags, F_INTERACTIVE) ? "" : "-d",
			cfg ? "-c" : "",
			cfg ? cfg : "",
			has_flag(flags, F_VERBOSE) ? "-v" : "",
			argc > 0 ? argv[0] : ""
		};

		// Unlock configuration
		conf_read_unlock();

		// Execute command
		if (has_flag(flags, F_INTERACTIVE)) {
			printf("control: Running in interactive mode.\n");
			fflush(stderr);
			fflush(stdout);
		}
		if ((rc = exec_cmd(args, 6)) < 0) {
			pid_remove(pidfile);
			rc = 1;
		}
		fflush(stderr);
		fflush(stdout);

		// Wait for finish
		if (has_flag(flags, F_WAIT) && !has_flag(flags, F_INTERACTIVE)) {
			if (has_flag(flags, F_VERBOSE)) {
				fprintf(stdout, "control: waiting for server "
						"to load.\n");
			}
			/* Periodically read pidfile and wait for
			 * valid result. */
			pid = 0;
			while(pid == 0 || !pid_running(pid)) {
				pid = pid_read(pidfile);
				struct timeval tv;
				tv.tv_sec = 0;
				tv.tv_usec = 500 * 1000;
				select(0, 0, 0, 0, &tv);
			}
		}
	}
	if (strcmp(action, "stop") == 0) {

		// Check PID
		valid_cmd = 1;
		rc = 0;
		if (pid <= 0 || !pid_running(pid)) {
			fprintf(stderr, "Server PID not found, "
			        "probably not running.\n");

			if (!has_flag(flags, F_FORCE)) {
				rc = 1;
			} else {
				fprintf(stderr, "control: forcing "
				        "server stop.\n");
			}
		}

		// Stop
		if (rc == 0) {
			if (kill(pid, SIGTERM) < 0) {
				pid_remove(pidfile);
				rc = 1;
			}
		}

		// Wait for finish
		if (rc == 0 && has_flag(flags, F_WAIT)) {
			if (has_flag(flags, F_VERBOSE)) {
				fprintf(stdout, "control: waiting for server "
						"to stop.\n");
			}
			/* Periodically read pidfile and wait for
			 * valid result. */
			while(pid_running(pid)) {
				struct timeval tv;
				tv.tv_sec = 0;
				tv.tv_usec = 500 * 1000;
				select(0, 0, 0, 0, &tv);
			}
		}
	}
	if (strcmp(action, "restart") == 0) {
		valid_cmd = 1;
		execute("stop", argv, argc, pid, flags, jobs, pidfile);

		int i = 0;
		while((pid = pid_read(pidfile)) > 0) {

			if (!pid_running(pid)) {
				pid_remove(pidfile);
				break;
			}
			if (i == WAITPID_TIMEOUT) {
				fprintf(stderr, "Timeout while "
				        "waiting for the server to finish.\n");
				//pid_remove(pidfile);
				break;
			} else {
				sleep(1);
				++i;
			}
		}

		printf("Restarting server.\n");
		rc = execute("start", argv, argc, -1, flags, jobs, pidfile);
	}
	if (strcmp(action, "reload") == 0) {

		// Check PID
		valid_cmd = 1;
		if (pid <= 0 || !pid_running(pid)) {
			fprintf(stderr, "Server PID not found, "
			        "probably not running.\n");

			if (has_flag(flags, F_FORCE)) {
				fprintf(stderr, "control: forcing "
				        "server stop.\n");
			} else {
				return 1;
			}
		}

		// Recompile zones if needed
		if (has_flag(flags, F_AUTO)) {
			rc = execute("compile", argv, argc, -1, flags,
			             jobs, pidfile);
		}

		// Stop
		if (kill(pid, SIGHUP) < 0) {
			pid_remove(pidfile);
			rc = 1;
		}
	}
	if (strcmp(action, "running") == 0) {

		// Check PID
		valid_cmd = 1;
		if (pid <= 0) {
			printf("Server PID not found, "
			       "probably not running.\n");
			rc = 1;
		} else {
			if (!pid_running(pid)) {
				printf("Server PID not found, "
				       "probably not running.\n");
				fprintf(stderr,
				        "warning: PID file is stale.\n");
			} else {
				printf("Server running as PID %ld.\n",
				       (long)pid);
			}
			rc = 0;
		}
	}
	if (strcmp(action, "checkconf") == 0) {
		rc = 0;
		valid_cmd = 1;
	}
	if (strcmp(action, "compile") == 0) {
		
		// Print job count
		if (jobs > 1) {
			printf("warning: Will attempt to compile %d zones "
			       "in parallel, this increases memory consumption "
			       "for large zones.\n", jobs);
		}

		// Check zone
		valid_cmd = 1;

		// Lock configuration
		conf_read_lock();

		// Generate databases for all zones
		node *n = 0;
		int running = 0;
		knotc_zctask_t *tasks = zctask_create(jobs);
		WALK_LIST(n, conf()->zones) {

			// Fetch zone
			conf_zone_t *zone = (conf_zone_t*)n;

			// Check source files and mtime
			int zone_status = check_zone(zone->db, zone->file);
			if (zone_status == KNOTD_EOK) {
				printf("Zone '%s' is up-to-date.\n",
				       zone->name);

				if (has_flag(flags, F_FORCE)) {
					fprintf(stderr, "control: forcing "
						"zone recompilation.\n");
				} else {
					continue;
				}
			}

			// Check for not existing source
			if (zone_status == KNOTD_ENOENT) {
				continue;
			}
			
			/* Evaluate space for new task. */
			if (running == jobs) {
				rc |= zctask_wait(tasks, jobs);
				--running;
			}

			const char *args[] = {
				ZONEPARSER_EXEC,
				zone->enable_checks ? "-s" : "",
				has_flag(flags, F_VERBOSE) ? "-v" : "",
				"-o",
				zone->db,
			        zone->name,
				zone->file
			};

			// Execute command
			if (has_flag(flags, F_VERBOSE)) {
				printf("Compiling '%s' as '%s'...\n",
				       zone->name, zone->db);
			}
			fflush(stdout);
			fflush(stderr);
			pid_t zcpid = start_cmd(args, 7);
			zctask_add(tasks, jobs, zcpid, zone);
			++running;
		}
		
		/* Wait for all running tasks. */
		while (running > 0) {
			rc |= zctask_wait(tasks, jobs);
			--running;
		}
		free(tasks);

		// Unlock configuration
		conf_read_unlock();
	}
	if (!valid_cmd) {
		fprintf(stderr, "Invalid command: '%s'\n", action);
		return 1;
	}

	// Log
	if (has_flag(flags, F_VERBOSE)) {
		printf("'%s' finished (return code %d)\n", action, rc);
	}
	return rc;
}

int main(int argc, char **argv)
{
	// Parse command line arguments
	int c = 0, li = 0;
	unsigned jobs = 1;
	unsigned flags = F_NULL;
	const char* config_fn = 0;
	
	/* Long options. */
	struct option opts[] = {
		{"wait",        no_argument,       0, 'w'},
		{"force",       no_argument,       0, 'f'},
		{"config",      required_argument, 0, 'c'},
		{"verbose",     no_argument,       0, 'v'},
		{"interactive", no_argument,       0, 'i'},
		{"auto",        no_argument,       0, 'a'},
		{"jobs",        required_argument, 0, 'j'},
		{"version",     no_argument,       0, 'V'},
		{"help",        no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};
	
	while ((c = getopt_long(argc, argv, "wfc:viaj:Vh", opts, &li)) != -1) {
		switch (c) {
		case 'w': flags |= F_WAIT; break;
		case 'f': flags |= F_FORCE; break;
		case 'v': flags |= F_VERBOSE; break;
		case 'i': flags |= F_INTERACTIVE; break;
		case 'a': flags |= F_AUTO; break;
		case 'c':
			config_fn = optarg;
			break;
		case 'j':
			jobs = atoi(optarg);
			if (jobs < 1) {
				fprintf(stderr, "Invalid parameter '%s' to "
				        "'-j', expects number <1..n>\n",
				        optarg);
				help(argc, argv);
				return 1;
			}
			break;
		case 'V':
			printf("%s, version %s\n", "Knot DNS", PACKAGE_VERSION);
			return 0;
		case 'h':
		case '?':
		default:
			help(argc, argv);
			return 1;
		}
	}

	// Check if there's at least one remaining non-option
	if (argc - optind < 1) {
		help(argc, argv);
		return 1;
	}

	// Initialize log (no output)
	log_init();
	log_levels_set(LOGT_SYSLOG, LOG_ANY, 0);
	log_levels_set(LOGT_STDOUT, LOG_ANY, 0);
	closelog();

	// Find implicit configuration file
	char *default_fn = 0;
	if (!config_fn) {
		default_fn = conf_find_default();
		config_fn = default_fn;
	}

	// Open configuration
	int conf_ret = conf_open(config_fn);
	if (conf_ret != KNOTD_EOK) {
		if (conf_ret == KNOTD_ENOENT) {
			fprintf(stderr, "Couldn't open configuration file "
				"'%s'.\n", config_fn);
		} else {
			fprintf(stderr, "Failed to parse configuration '%s'.\n",
				config_fn);
		}
		free(default_fn);
		return 1;
	}

	// Free default config filename if exists
	free(default_fn);

	// Verbose mode
	if (has_flag(flags, F_VERBOSE)) {
		log_levels_add(LOGT_STDOUT, LOG_ANY,
		               LOG_MASK(LOG_INFO)|LOG_MASK(LOG_DEBUG));
	}

	// Fetch PID
	char* pidfile = pid_filename();
	if (!pidfile) {
		fprintf(stderr, "No configuration found, "
		        "please specify with '-c' parameter.\n");
		log_close();
		return 1;
	}

	pid_t pid = pid_read(pidfile);

	// Actions
	const char* action = argv[optind];

	// Execute action
	int rc = execute(action, argv + optind + 1, argc - optind - 1,
	                 pid, flags, jobs, pidfile);

	// Finish
	free(pidfile);
	log_close();
	return rc;
}
