#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include "Arduino.h"

// max cpu usage, throttle with -t
#define DEF_CPU_USAGE 0.8F
static float max_cpu_usage = DEF_CPU_USAGE;

char **our_argv;                // our argv for restarting
std::string our_dir;            // our storage directory, including trailing /

// list of diagnostic files, newest first
const char *diag_files[N_DIAG_FILES] = {
    "diagnostic-log.txt",
    "diagnostic-log-0.txt",
    "diagnostic-log-1.txt",
    "diagnostic-log-2.txt"
};


// how we were made
#if defined(_USE_FB0)
  #if defined(_CLOCK_1600x960)
      char our_make[] = "hamclock-fb0-1600x960";
  #elif defined(_CLOCK_2400x1440)
      char our_make[] = "hamclock-fb0-2400x1440";
  #elif defined(_CLOCK_3200x1920)
      char our_make[] = "hamclock-fb0-3200x1920";
  #else
      char our_make[] = "hamclock-fb0-800x480";
  #endif
#elif defined(_USE_X11)
  #if defined(_CLOCK_1600x960)
      char our_make[] = "hamclock-1600x960";
  #elif defined(_CLOCK_2400x1440)
      char our_make[] = "hamclock-2400x1440";
  #elif defined(_CLOCK_3200x1920)
      char our_make[] = "hamclock-3200x1920";
  #else
      char our_make[] = "hamclock-800x480";
  #endif
#elif defined(_WEB_ONLY)
  #if defined(_CLOCK_1600x960)
      char our_make[] = "hamclock-web-1600x960";
  #elif defined(_CLOCK_2400x1440)
      char our_make[] = "hamclock-web-2400x1440";
  #elif defined(_CLOCK_3200x1920)
      char our_make[] = "hamclock-web-3200x1920";
  #else
      char our_make[] = "hamclock-web-800x480";
  #endif
#else
  #error Unknown build configuration
#endif
 
static const char *pw_file;

/* return milliseconds since first call
 */
uint32_t millis(void)
{
    #if defined(CLOCK_MONOTONIC_FAST_XXXX)
        // this is only on FreeBSD but is fully 200x faster than gettimeofday or CLOCK_MONOTONIC
        // as of 14-RELEASE now this one is slow -- use normal gettimeofday
	static struct timespec t0;
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC_FAST, &t);
	if (t0.tv_sec == 0)
	    t0 = t;
	uint32_t dt_ms = (t.tv_sec - t0.tv_sec)*1000 + (t.tv_nsec - t0.tv_nsec)/1000000;
	return (dt_ms);
    #else
	static struct timeval t0;
	struct timeval t;
	gettimeofday (&t, NULL);
	if (t0.tv_sec == 0)
	    t0 = t;
	uint32_t dt_ms = (t.tv_sec - t0.tv_sec)*1000 + (t.tv_usec - t0.tv_usec)/1000;
	return (dt_ms);
    #endif
}

void delay (uint32_t ms)
{
	usleep (ms*1000);
}

long random(int max)
{
        return ((::random() >> 3) % max);
}

uint16_t analogRead(int pin)
{
	return (0);		// not supported on Pi, consider https://www.adafruit.com/product/1083
}

static void mvLog (const char *from, const char *to)
{
        std::string from_path = our_dir + from;
        std::string to_path = our_dir + to;
        const char *from_fn = from_path.c_str();
        const char *to_fn = to_path.c_str();
        if (rename (from_fn, to_fn) < 0 && errno != ENOENT) {
            // fails for a reason other than from does not exist
            fprintf (stderr, "rename(%s,%s): %s\n", from_fn, to_fn, strerror(errno));
            exit(1);
        }
}


/* roll diag files and divert stdout and stderr to fresh file in our_dir
 */
static void makeDiagFile()
{
        // roll previous few
        for (int i = N_DIAG_FILES-1; i > 0; --i)
            mvLog (diag_files[i-1], diag_files[i]);

        // reopen stdout as new log
        std::string new_log = our_dir + diag_files[0];
        const char *new_log_fn = new_log.c_str();
        int logfd = open (new_log_fn, O_WRONLY|O_CREAT, 0664);
        if (logfd < 0 || ::dup2(logfd, 1) < 0 || ::dup2(logfd, 2) < 0) {
            fprintf (stderr, "%s: %s\n", new_log_fn, strerror(errno));
            exit(1);
        }
        (void) !fchown (logfd, getuid(), getgid());

        // original fd no longer needed
        close (logfd);
}

/* return default working directory
 */
static std::string defaultAppDir()
{
        std::string home = getenv ("HOME");
        return (home + "/.hamclock/");
}

/* like mkdir() but checks/creates all intermediate components
 */
static int mkpath (const char *path, int mode)
{
        mode_t old_um = umask(0);
        char *path_copy = strdup(path);
        char *p = path_copy;
        int ok, atend, atslash;

        do {
            atend = (*p == '\0');
            atslash = (*p == '/' && p > path_copy);
            ok = 1;
            if (atend || atslash) {
                *p = '\0';
                if (mkdir (path_copy, mode) == 0) {
                    if (chown (path_copy, getuid(), getgid()) < 0)
                        ok = 0;
                } else if (errno != EEXIST)
                    ok = 0;
                if (atslash)
                    *p = '/';
            }
            p++;
        } while (ok && !atend);

        free (path_copy);
        umask(old_um);

        return (ok ? 0 : -1);
}


/* insure our application work directory exists and named in our_dir.
 * use default unless user_dir.
 * exit if trouble.
 */
static void mkAppDir(const char *user_dir)
{
        // use user_dir else default
        if (user_dir) {
            our_dir = user_dir;
            // insure ends with /
            if (our_dir.compare (our_dir.length()-1, 1, "/")) {
                std::string slash = "/";
                our_dir = our_dir + slash;
            }
        } else {
            // use default
            our_dir = defaultAppDir();
        }

        // insure exists, fine if already created
        const char *path = our_dir.c_str();
        if (mkpath (path, 0775) < 0 && errno != EEXIST) {
            // EEXIST just means it already exists
            fprintf (stderr, "%s: %s\n", path, strerror(errno));
            exit(1);
        }
}

/* convert the given ISO 8601 date-time string to UNIX seconds in usr_datetime.
 * bale if bad format.
 */
static void setUsrDateTime (const char *iso8601)
{
        int yr, mo, dy, hr, mn, sc;
        if (sscanf (iso8601, "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) != 6) {
            fprintf (stderr, "-s format not recognized\n");
            exit(1);
        }

        struct tm tms;
        memset (&tms, 0, sizeof(tms));
        tms.tm_year = yr - 1900;                // wants year - 1900
        tms.tm_mon = mo - 1;                    // wants month 0..11
        tms.tm_mday = dy;
        tms.tm_hour = hr;
        tms.tm_min = mn;
        tms.tm_sec = sc;
        setenv ("TZ", "UTC0", 1);               // UTC
        tzset();
        usr_datetime = mktime (&tms);
}

/* log easy OS info
 */
static void logOS()
{
        const char osf[] = "/etc/os-release";
        FILE *fp = fopen (osf, "r");
        if (fp) {
            char line[100];
            printf ("%s:\n", osf);
            while (fgets (line, sizeof(line), fp))
                printf ("    %s", line);        // line already includes \n
            fclose(fp);
        }

        (void) system ("uname -a");
}

/* show version info
 */
static void showVersion()
{
        fprintf (stderr, "Version %s\n", hc_version);
        fprintf (stderr, "built as %s\n", our_make);
}

/* show usage and exit(1)
 */
static void usage (const char *errfmt, ...)
{
        char *slash = strrchr (our_argv[0], '/');
        char *me = slash ? slash+1 : our_argv[0];

        if (errfmt) {
            va_list ap;
            va_start (ap, errfmt);
            fprintf (stderr, "Usage error: ");
            vfprintf (stderr, errfmt, ap);
            va_end (ap);
            if (!strchr(errfmt, '\n'))
                fprintf (stderr, "\n");
        }

        fprintf (stderr, "Purpose: display time and other information useful to amateur radio operators\n");
        fprintf (stderr, "Usage: %s [options]\n", me);
        fprintf (stderr, "Options:\n");
        fprintf (stderr, " -a l : set gimbal trace level\n");
        fprintf (stderr, " -b h : set backend host:port to h; default is %s:%d\n", backend_host,backend_port);
        fprintf (stderr, " -c   : disable all touch events from web interface\n");
        fprintf (stderr, " -d d : set working directory to d; default is %s\n", defaultAppDir().c_str());
        fprintf (stderr, " -e p : set RESTful web server port to p or -1 to disable; default %d\n", RESTFUL_PORT);

        fprintf (stderr, " -f o : force display full screen initially to \"on\" or \"off\"\n");
        fprintf (stderr, " -g   : init DE using geolocation with current public IP; requires -k\n");
        fprintf (stderr, " -h   : print this help summary then exit\n");
        fprintf (stderr, " -i i : init DE using geolocation with IP i; requires -k\n");
        fprintf (stderr, " -k   : start immediately in normal mode, ie, don't offer Setup or wait for Skips\n");
        fprintf (stderr, " -l l : set Mercator or Mollweide center longitude to l degrees, +E; requires -k\n");
        fprintf (stderr, " -m   : enable demo mode\n");
        fprintf (stderr, " -o   : write diagnostic log to stdout instead of in %s\n",defaultAppDir().c_str());
        fprintf (stderr, " -p f : require passwords in file f formatted as lines of \"category password\"\n");
        fprintf (stderr, "        categories: changeUTC exit newde newdx reboot restart setup shutdown unlock upgrade\n");
        fprintf (stderr, " -s d : start time as if UTC now is d formatted as YYYY-MM-DDTHH:MM:SS\n");
        fprintf (stderr, " -t p : throttle max cpu to p percent; default is %.0f\n", DEF_CPU_USAGE*100);
        fprintf (stderr, " -v   : show version info then exit\n");
        fprintf (stderr, " -w p : set live web server port to p or -1 to disable; default %d\n",LIVEWEB_PORT);
        fprintf (stderr, " -y   : activate keyboard cursor control arrows/hjkl/Return -- beware stuck keys!\n");

        exit(1);
}

/* process main's argc/argv -- never returns if any issues
 */
static void crackArgs (int ac, char *av[])
{
        bool diag_to_file = true;
        bool full_screen = false;
        bool fs_set = false;
        const char *new_appdir = NULL;
        bool cl_set = false;

         while (--ac && **++av == '-') {
            char *s = *av;
            while (*++s) {
                switch (*s) {
                case 'a':
                    if (ac < 2)
                        usage ("missing trace level for -a");
                    gimbal_trace_level = atoi(*++av);
                    ac--;
                    break;
                case 'b': {
                        if (ac < 2)
                            usage ("missing host name for -b");
                        char *bh = strdup(*++av);           // copy so we don't modify av[]
                        backend_host = bh;
                        char *colon = strchr (bh, ':');
                        if (colon) {
                            *colon = '\0';
                            backend_port = atoi(colon+1);
                            if (backend_port < 1 || backend_port > 65535)
                                usage ("-b port must be [1,65355]");
                        }
                        ac--;
                    }
                    break;
                case 'c':
                    no_web_touch = true;
                    break;
                case 'd':
                    if (ac < 2)
                        usage ("missing directory path for -d");
                    new_appdir = *++av;
                    ac--;
                    break;
                case 'e':
                    if (ac < 2)
                        usage ("missing RESTful port number for -e");
                    restful_port = atoi(*++av);
                    if (restful_port != -1 && (restful_port < 1 || restful_port > 65535))
                        usage ("-e port must be -1 or [1,65355]");
                    ac--;
                    break;
                case 'f':
                    if (ac < 2) {
                        usage ("missing arg for -f");
                    } else {
                        char *oo = *++av;
                        if (strcmp (oo, "on") == 0 || strcmp (oo, "yes") == 0)
                            full_screen = true;
                        else if (strcmp (oo, "off") == 0 || strcmp (oo, "no") == 0)
                            full_screen = false;
                        else
                            usage ("-f requires on or off");
                        ac--;
                        fs_set = true;
                    }
                    break;
                case 'g':
                    init_iploc = true;
                    break;
                case 'h':
                    usage (NULL);
                    break;
                case 'i':
                    if (ac < 2)
                        usage ("missing IP for -i");
                    init_locip = *++av;
                    ac--;
                    break;
                case 'k':
                    skip_skip = true;
                    break;
                case 'l':
                    if (ac < 2)
                        usage ("missing longitude for -l");
                    setCenterLng(atoi(*++av));
                    cl_set = true;
                    ac--;
                    break;
                case 'm':
                    setDemoMode(true);
                    break;
                case 'o':
                    diag_to_file = false;
                    break;
                    break;
                case 'p':
                    if (ac < 2)
                        usage ("missing file name for -p");
                    pw_file = *++av;
                    ac--;
                    break;
                case 's':
                    if (ac < 2)
                        usage ("missing date/time for -s");
                    setUsrDateTime(*++av);
                    ac--;
                    break;
                case 't':
                    if (ac < 2)
                        usage ("missing percentage for -t");
                    max_cpu_usage = atoi (*++av)/100.0F;
                    if (max_cpu_usage <0.1F || max_cpu_usage>1)
                        usage ("-t percentage must be 10 .. 100");
                    ac--;
                    break;
                case 'v':
                    showVersion();
                    exit(0);
                    break;      // lint
                case 'w':
                    if (ac < 2)
                        usage ("missing web port number for -w");
                    liveweb_port = atoi(*++av);
                    if (liveweb_port != -1 && (liveweb_port < 1 || liveweb_port > 65535))
                        usage ("-w port must be -1 or [1,65535");
                    ac--;
                    break;
                case 'x':
                    usage ("-x is no longer supported -- replaced with direct web \"make\" targets");
                    break;
                case 'y':
                    want_kbcursor = true;
                    break;
                default:
                    usage ("unknown option: %c", *s);
                }
            }
        }

        // initial checks
        if (ac > 0)
            usage ("extra args");
        if (init_iploc && init_locip)
            usage ("can not use both -g and -i");
        if (init_iploc && !skip_skip)
            usage ("-g requires -k");
        if (init_locip && !skip_skip)
            usage ("-i requires -k");
        if (cl_set && !skip_skip)
            usage ("-l requires -k");
        if (liveweb_port == restful_port && liveweb_port > 0 && restful_port > 0)
            usage ("Live web and RESTful ports may not be equal: %d %d", liveweb_port, restful_port);


        // prepare our working directory in our_dir
        mkAppDir (new_appdir);

        // redirect stdout to diag file unless requested not to
        if (diag_to_file)
            makeDiagFile();

        // set desired screen option if set
        if (fs_set)
            setX11FullScreen (full_screen);
}

/* Every normal C program requires a main().
 * This is provided as magic in the Arduino IDE so here we must do it ourselves.
 */
int main (int ac, char *av[])
{
	// save our args for identical restart or remote update
	our_argv = av;

        // always want stdout synchronous 
        setbuf (stdout, NULL);

        // check args
        crackArgs (ac, av);

        // log args after cracking so they go to proper diag file
        printf ("\nNew program args:\n");
        for (int i = 0; i < ac; i++)
            printf ("  argv[%d] = %s\n", i, av[i]);

        // log our some info
        printf ("process id %d\n", getpid());
        printf ("built as %s\n", our_make);
        printf ("working directory is %s\n", our_dir.c_str());
        printf ("ruid %d euid %d\n", getuid(), geteuid());
        if (pw_file)
            capturePasswords (pw_file);

        // log os release, if available
        logOS();

	// call Arduino setup one time
        printf ("Calling Arduino setup()\n");
	setup();

        // performance measurements
        int cpu_us = 0, et_us = 0;      // cpu and elapsed time
        int sleep_us = 100;             // initial sleep, usecs
        const int sleep_dt = 10;        // sleep adjustment, usecs
        const int max_sleep = 50000;    // max sleep each loop, usecs

        #define TVUSEC(tv0,tv1) (((tv1).tv_sec-(tv0).tv_sec)*1000000 + ((tv1).tv_usec-(tv0).tv_usec))

	// call Arduino loop forever
        // this loop by itself would run 100% CPU so try to be a better citizen and throttle back
        printf ("Starting Arduino loop()\n");
	for (;;) {

            // get time and usage before calling loop()
            struct rusage ru0;
            getrusage (RUSAGE_SELF, &ru0);
            struct timeval tv0;
            gettimeofday (&tv0, NULL);

            // Ardino loop
	    loop();

            if (max_cpu_usage < 1) {
                // cap cpu usage by sleeping controlled by a simple integral controller
                if (cpu_us > et_us*max_cpu_usage) {
                    // back off
                    if (sleep_us < max_sleep)
                        sleep_us += sleep_dt;
                } else {
                    // more!
                    if (sleep_us < sleep_dt)
                        sleep_us = 0;
                    else
                        sleep_us -= sleep_dt;
                }
                if (sleep_us > 0)
                    usleep (sleep_us);

                // get time and usage after running loop() and our usleep
                struct rusage ru1;
                getrusage (RUSAGE_SELF, &ru1);
                struct timeval tv1;
                gettimeofday (&tv1, NULL);

                // find cpu time used
                struct timeval &ut0 = ru0.ru_utime;
                struct timeval &ut1 = ru1.ru_utime;
                struct timeval &st0 = ru0.ru_stime;
                struct timeval &st1 = ru1.ru_stime;
                int ut_us = TVUSEC(ut0,ut1);
                int st_us = TVUSEC(st0,st1);
                cpu_us = ut_us + st_us;

                // find elapsed time
                et_us = TVUSEC(tv0,tv1);

                // printf ("sleep_us= %10d cpu= %10d et= %10d %g\n", sleep_us, cpu_us, et_us, fmin(100,100.0*cpu_us/et_us));
            }
	}
}
