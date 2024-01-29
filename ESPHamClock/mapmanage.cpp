/* this file manages the background maps, both static styles and VOACAP area propagation.
 *
 * On ESP:
 *    maps are stored in a LittleFS file system, pixels accessed with file.seek and file.read with a cache
 * On all desktops:
 *    maps are stored in $HOME/.hamclock, pixels accessed with mmap
 *
 * all map files are RGB565 BMP V4 format.
 */


#include "HamClock.h"



// BMP file format parameters
#define COREHDRSZ 14                                    // always 14 bytes at front of header
#define HDRVER 108                                      // BITMAPV4HEADER, these many more bytes in subheader
#define BHDRSZ (COREHDRSZ+HDRVER)                       // total header size
#define BPERBMPPIX 2                                    // bytes per BMP pixel

// box in which to draw map scales
SBox mapscale_b;

// current CoreMap designation even if not currently being shown, if any
CoreMaps core_map = CM_NONE;                            // current core map, if any

// current VOACAP prop map setting, if any
PropMapSetting prop_map;


// central file name components for the core background maps -- not including voacap.
#define X(a,b)   b,                                     // expands COREMAPS to each name plus comma
const char *coremap_names[CM_N] = {
    COREMAPS
};
#undef X

// prop and muf style names
static const char prop_style[] = "PropMap";
static const char muf_style[] = "MUFMap";


#if defined(_IS_ESP8266)

/***********************************************************************************************
 *
 * Only on ESP
 *
 ***********************************************************************************************/

/* LittleFS seek+read performance: read_ms = 88 + 0.23/byte
 * Thus longer caches help mercator but greatly slow azimuthal due to cache misses. Whole row is
 * a huge loss for azimuthal.
 */

// persistant state
#define N_CACHE_COLS     50                             // n read-ahead columns to cache
static File day_file, night_file;                       // open LittleFS file handles
static uint8_t *day_row_cache, *night_row_cache;        // row caches
static uint16_t day_cache_row, night_cache_row;         // which starting rows
static uint16_t day_cache_col, night_cache_col;         // which starting cols


/* return day RGB565 pixel at the given location.
 * ESP only
 */
bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *dayp)
{
        // beware no map
        if (!day_row_cache) {
            *dayp = 0;
            return (false);
        }

        // report cache miss stats occasionally
        static int n_query, cache_miss;
        if (++n_query == 1000) {
            // Serial.printf ("day cache miss   %4d/%4d\n", cache_miss, n_query);
            cache_miss = n_query = 0;
        }

        if (row >= HC_MAP_H || col >= HC_MAP_W) {
            Serial.printf (_FX("%s: day %d %d out of bounds %dx%d\n"), row, col, HC_MAP_W, HC_MAP_H);
            return (false);
        }

        // update cache if miss
        if (row != day_cache_row || col < day_cache_col || col >= day_cache_col+N_CACHE_COLS) {
            cache_miss++;
            resetWatchdog();
            if (!day_file.seek (BHDRSZ + (row*HC_MAP_W+col)*BPERBMPPIX, SeekSet)
                        || !day_file.read (day_row_cache, BPERBMPPIX*N_CACHE_COLS)) {
                Serial.printf (_FX("day pixel read err at %d x %d\n"), row, col);
                return (false);
            }
            day_cache_row = row;
            day_cache_col = col;
        }

        // return value from cache
        int idx0 = (col-day_cache_col)*BPERBMPPIX;
        *dayp = *(uint16_t*)(&day_row_cache[idx0]);

        // ok
        return (true);
}


/* return night RGB565 pixel at the given location.
 * ESP only
 */
bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp)
{
        // beware no map
        if (!night_row_cache) {
            *nightp = 0;
            return (false);
        }

        // report cache miss stats occasionally
        static int n_query, cache_miss;
        if (++n_query == 1000) {
            // Serial.printf ("night cache miss   %4d/%4d\n", cache_miss, n_query);
            cache_miss = n_query = 0;
        }

        if (row >= HC_MAP_H || col >= HC_MAP_W) {
            Serial.printf (_FX("%s: night %d %d out of bounds %dx%d\n"), row, col, HC_MAP_W, HC_MAP_H);
            return (false);
        }

        // update cache if miss
        if (row != night_cache_row || col < night_cache_col || col >= night_cache_col+N_CACHE_COLS) {
            cache_miss++;
            resetWatchdog();
            if (!night_file.seek (BHDRSZ + (row*HC_MAP_W+col)*BPERBMPPIX, SeekSet)
                        || !night_file.read (night_row_cache, BPERBMPPIX*N_CACHE_COLS)) {
                Serial.printf (_FX("night pixel read err at %d x %d\n"), row, col);
                return (false);
            }
            night_cache_row = row;
            night_cache_col = col;
        }

        // return value from cache
        int idx0 = (col-night_cache_col)*BPERBMPPIX;
        *nightp = *(uint16_t*)(&night_row_cache[idx0]);

        // ok
        return (true);
}

/* invalidate pixel connection until proven good again
 * ESP only
 */
static void invalidatePixels()
{
        if (day_row_cache) {
            free (day_row_cache);
            day_row_cache = NULL;
        }
        if (night_row_cache) {
            free (night_row_cache);
            night_row_cache = NULL;
        }
}

/* prepare open day_file and night_file for pixel access.
 * if trouble close both and return false, else return true.
 * ESP only
 */
static bool installFilePixels (const char *dfile, const char *nfile)
{
        // check files are indeed open
        if (!day_file || !night_file) {

            // note and close the open file(s)
            if (day_file)
                day_file.close();
            else
                Serial.printf (_FX("%s not open\n"), dfile);
            if (night_file)
                night_file.close();
            else
                Serial.printf (_FX("%s not open\n"), nfile);

            // bad
            return (false);
        }

        // init row caches for getMapDay/NightPixel()
        day_row_cache = (uint8_t *) realloc (day_row_cache, BPERBMPPIX*N_CACHE_COLS);
        night_row_cache = (uint8_t *) realloc (night_row_cache, BPERBMPPIX*N_CACHE_COLS);
        if (!day_row_cache || !night_row_cache)
            fatalError ("pixel cache %p %p", day_row_cache, night_row_cache);   // no _FX if alloc failing
        day_cache_col = day_cache_row = ~0;     // mark as invalid
        night_cache_col = night_cache_row = ~0;

        // ok!
        return (true);
}

/* qsort-style compare two FS_Info by UNIX time
 * ESP only
 */
static int FSInfoTimeQsort (const void *p1, const void *p2)
{
        time_t t1 = ((FS_Info *)p1)->t0;
        time_t t2 = ((FS_Info *)p2)->t0;

        if (t1 < t2)
            return (-1);
        if (t1 > t2)
            return (1);
        return(0);
}

/* ESP FLASH can only hold 4 map files, remove some if necessary to make room for specied number.
 * ESP only
 */
static void cleanFLASH (const char *title, int need_files)
{
        resetWatchdog();

        // max number of existing files allowable
        int max_ok = 4 - need_files;

        // get info on existing files
        uint64_t fs_size, fs_used;
        char *fs_name;
        int n_files;
        FS_Info *fsi = getConfigDirInfo (&n_files, &fs_name, &fs_size, &fs_used);

        // always remove muf map because it is always refreshed
        for (int i = 0; i < n_files; i++) {
            FS_Info *fip = &fsi[i];
            if (strstr(fip->name, muf_style)) {
                Serial.printf (_FX("%s: rm %s\n"), title, fip->name);
                LittleFS.remove (fip->name);
            }
        }

        // recheck
        free (fs_name);
        free (fsi);
        fsi = getConfigDirInfo (&n_files, &fs_name, &fs_size, &fs_used);
        if (n_files > max_ok) {

            // still too many. sort by time, oldest first
            qsort (fsi, n_files, sizeof(*fsi), FSInfoTimeQsort);

            // remove oldest until enough room
            for (int i = 0; i < n_files && n_files-i > max_ok; i++) {
                FS_Info *fip = &fsi[i];
                Serial.printf (_FX("%s: rm %s\n"), title, fip->name);
                LittleFS.remove (fip->name);
            }

        }

        // should be ok
        free (fs_name);
        free (fsi);
}


#else   // !_IS_ESP8266



/***********************************************************************************************
 *
 * only on UNIX
 *
 ***********************************************************************************************/



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>


// persistent state of open files, allows restarting
static File day_file, night_file;                       // open LittleFS file handles
static int day_fbytes, night_fbytes;                    // bytes mmap'ed
static char *day_pixels, *night_pixels;                 // pixels mmap'ed


// dummies for linking
bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *nightp) { return (false); }
bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp) { return (false); }
static void cleanFLASH (const char *title, int n) {}


/* invalidate pixel connection until proven good again
 * UNIX only
 */
static void invalidatePixels()
{
        // disconnect from tft thread
        tft.setEarthPix (NULL, NULL);

        // unmap pixel arrays
        if (day_pixels) {
            munmap (day_pixels, day_fbytes);
            day_pixels = NULL;
        }
        if (night_pixels) {
            munmap (night_pixels, day_fbytes);
            night_pixels = NULL;
        }
}

/* prepare open day_file and night_file for pixel access.
 * return whether ok
 * UNIX only
 */
static bool installFilePixels (const char *dfile, const char *nfile)
{
        bool ok = false;

        // mmap pixels if both files are open
        if (day_file && night_file) {

            day_fbytes = BHDRSZ + HC_MAP_W*HC_MAP_H*2;          // n bytes of 16 bit RGB565 pixels
            night_fbytes = BHDRSZ + HC_MAP_W*HC_MAP_H*2;
            day_pixels = (char *)                               // allow OS to choose addrs
                    mmap (NULL, day_fbytes, PROT_READ, MAP_PRIVATE, day_file.fileno(), 0);
            night_pixels = (char *)
                    mmap (NULL, night_fbytes, PROT_READ, MAP_PRIVATE, night_file.fileno(), 0);

            ok = day_pixels != MAP_FAILED && night_pixels != MAP_FAILED;
        }

        // install pixels if ok
        if (ok) {

            // Serial.println (F("both mmaps good"));

            // don't need files open once mmap has been established
            day_file.close();
            night_file.close();;

            // install in tft at start of pixels
            tft.setEarthPix (day_pixels+BHDRSZ, night_pixels+BHDRSZ);

        } else {

            // no go -- clean up

            if (day_file)
                day_file.close();
            else
                Serial.printf (_FX("%s not open\n"), dfile);
            if (day_pixels == MAP_FAILED)
                Serial.printf (_FX("%s mmap failed: %s\n"), dfile, strerror(errno));
            else if (day_pixels)
                munmap (day_pixels, day_fbytes);
            day_pixels = NULL;

            if (night_file)
                night_file.close();
            else
                Serial.printf (_FX("%s not open\n"), nfile);
            if (night_pixels == MAP_FAILED)
                Serial.printf (_FX("%s mmap failed: %s\n"), nfile, strerror(errno));
            else if (night_pixels)
                munmap (night_pixels, night_fbytes);
            night_pixels = NULL;

        }

        return (ok);
}

#endif // _IS_ESP8266



/***********************************************************************************************
 *
 * remaining functions are common to both architectures
 *
 ***********************************************************************************************/



/* don't assume we can access unaligned 32 bit values
 */
static uint32_t unpackLE4 (char *buf)
{
        union {
            uint32_t le4;
            char a[4];
        } le4;

        le4.a[0] = buf[0];
        le4.a[1] = buf[1];
        le4.a[2] = buf[2];
        le4.a[3] = buf[3];

        return (le4.le4);
}

/* return whether the given header is the correct BMP format and the total expected file size.
 */
static bool bmpHdrOk (char *buf, uint32_t w, uint32_t h, uint32_t *filesizep)
{
        if (buf[0] != 'B' || buf[1] != 'M') {
            Serial.printf ("Hdr err: ");
            for (int i = 0; i < 10; i++)
                Serial.printf ("0x%02X %c, ", (unsigned)buf[i], (unsigned char)buf[i]);
            Serial.printf ("\n");
            return (false);
        }

        *filesizep = unpackLE4(buf+2);
        uint32_t type = unpackLE4(buf+14);
        uint32_t nrows = - (int32_t)unpackLE4(buf+22);          // nrows<0 means display upside down
        uint32_t ncols = unpackLE4(buf+18);
        uint32_t pixbytes = unpackLE4(buf+34);

        if (pixbytes != nrows*ncols*BPERBMPPIX || type != HDRVER || w != ncols || h != nrows) {
            Serial.printf (_FX("Hdr err: %d %d %d %d\n"), pixbytes, type, nrows, ncols);
            return (false);
        }

        return (true);
}


/* marshall the day and night file names and titles for the given style.
 * N.B. we do not check for suffient room in the arrays
 * N.B. CM_DRAP name adds -S for no-scale version as of HamClock V2.67
 * N.B. CM_WX name depends on useMetricUnits()
 */
static void buildMapNames (const char *style, char *dfile, char *nfile, char *dtitle, char *ntitle)
{
        if (strcmp (style, "DRAP") == 0) {
            snprintf (dfile, 32, _FX("/map-D-%dx%d-DRAP-S.bmp"), HC_MAP_W, HC_MAP_H);
            snprintf (nfile, 32, _FX("/map-N-%dx%d-DRAP-S.bmp"), HC_MAP_W, HC_MAP_H);
        } else if (strcmp (style, "Weather") == 0) {
            const char *units = useMetricUnits() ? "mB" : "in";
            snprintf (dfile, 32, _FX("/map-D-%dx%d-Weather-%s.bmp"), HC_MAP_W, HC_MAP_H, units);
            snprintf (nfile, 32, _FX("/map-N-%dx%d-Weather-%s.bmp"), HC_MAP_W, HC_MAP_H, units);
        } else {
            snprintf (dfile, 32, _FX("/map-D-%dx%d-%s.bmp"), HC_MAP_W, HC_MAP_H, style);
            snprintf (nfile, 32, _FX("/map-N-%dx%d-%s.bmp"), HC_MAP_W, HC_MAP_H, style);
        }

        snprintf (dtitle, NV_COREMAPSTYLE_LEN+10, _FX("%s D map"), style);
        snprintf (ntitle, NV_COREMAPSTYLE_LEN+10, _FX("%s N map"), style);
}

/* qsort-style compare two FS_Info by name
 */
static int FSInfoNameQsort (const void *p1, const void *p2)
{
        return (strcmp (((FS_Info *)p1)->name, ((FS_Info *)p2)->name));
}



/* download the given file of expected size and load into LittleFS.
 * client is already postioned at first byte of image.
 */
static bool downloadMapFile (WiFiClient &client, const char *file, const char *title)
{
        resetWatchdog();

        // set if all ok
        bool ok = false;

        // alloc copy buffer
        #define COPY_BUF_SIZE 1024                      // > BHDRSZ but beware RAM pressure
        const uint32_t npixbytes = HC_MAP_W*HC_MAP_H*BPERBMPPIX;
        uint32_t nbufbytes = 0;
        StackMalloc buf_mem(COPY_BUF_SIZE);
        char *copy_buf = (char *) buf_mem.getMem();

        // (re)create file
        // extra open/close/remove avoids LitteLFS duplicate COW behavior
        File f = LittleFS.open (file, "r");
        if (f) {
            f.close();
            LittleFS.remove(file);
        }
        f = LittleFS.open (file, "w");
        if (!f) {
            #if defined(_IS_ESP8266)
                // using fatalError would probably leave user stranded in what is likely a persistent err
                mapMsg (true, 1000, _FX("%s: create failed"), title);
                return (false);
            #else
                // use non-standard File members for richer error msg
                fatalError (_FX("Error creating required file:\n%s\n%s"), f.fpath.c_str(), f.errstr.c_str());
                // never returns
            #endif
        }

        // read and check remote header
        for (int i = 0; i < BHDRSZ; i++) {
            if (!getTCPChar (client, &copy_buf[i])) {
                Serial.printf (_FX("short header: %.*s\n"), i, copy_buf); // might be err message
                mapMsg (true, 1000, _FX("%s: header is short"), title);
                goto out;
            }
        }
        uint32_t filesize;
        if (!bmpHdrOk (copy_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            Serial.printf (_FX("bad header: %.*s\n"), BHDRSZ, copy_buf); // might be err message
            mapMsg (true, 1000, _FX("%s: bad header"), title);
            goto out;
        }
        if (filesize != npixbytes + BHDRSZ) {
            Serial.printf (_FX("%s: wrong size %u != %u\n"), title, filesize, npixbytes);
            mapMsg (true, 1000, _FX("%s: wrong size"), title);
            goto out;
        }

        // write header
        f.write (copy_buf, BHDRSZ);
        updateClocks(false);

        // copy pixels
        {   // statement block just to avoid complaint about goto bypassing t0
            mapMsg (false, 100, _FX("%s: downloading"), title);
            uint32_t t0 = millis();
            for (uint32_t nbytescopy = 0; nbytescopy < npixbytes; nbytescopy++) {

                if (((nbytescopy%(npixbytes/10)) == 0) || nbytescopy == npixbytes-1)
                    mapMsg (false, 0, _FX("%s: %3d%%"), title, 100*(nbytescopy+1)/npixbytes);

                // read more
                if (nbufbytes < COPY_BUF_SIZE && !getTCPChar (client, &copy_buf[nbufbytes++])) {
                    Serial.printf (_FX("%s: file is short: %u %u\n"), title, nbytescopy, npixbytes);
                    mapMsg (true, 1000, _FX("%s: file is short"), title);
                    goto out;
                }

                // write when copy_buf is full or last
                if (nbufbytes == COPY_BUF_SIZE || nbytescopy == npixbytes-1) {
                    resetWatchdog();
                    updateClocks(false);
                    if (f.write (copy_buf, nbufbytes) != nbufbytes) {
                        mapMsg (true, 1000, _FX("%s: write failed"), title);
                        goto out;
                    }
                    nbufbytes = 0;
                }
            }
            Serial.printf (_FX("%s: %ld B/s\n"), title, 1000L*npixbytes/(millis()-t0));
        }

        // if get here, it worked!
        ok = true;

    out:

        f.close();
        if (!ok)
            LittleFS.remove (file);

        return (ok);
}

/* crack Last-Modified: Tue, 29 Sep 2020 22:55:02
 * return whether parsing was successful.
 */
static bool crackLastModified (const char *line, time_t &time_val)
{
        char mstr[10];
        int dy, mo, yr, hr, mn, sc;
        if (sscanf (line, _FX("%*[^,], %d %3s %d %d:%d:%d"), &dy, mstr, &yr, &hr, &mn, &sc) == 6
                                                && crackMonth (mstr, &mo)) {
            tmElements_t tm;
            tm.Year = yr - 1970;
            tm.Month = mo;
            tm.Day = dy;
            tm.Hour = hr;
            tm.Minute = mn;
            tm.Second = sc;
            time_val = makeTime (tm);
            return (true);
        }
        return (false);
}

/* open the given file and confirm its size, downloading fresh if not found, no match or newer.
 * if successful return:
 *   with position offset at first pixel,
 *   indicate whether a file was downloaded,
 *   open LittleFS File
 * else return a closed File
 */
static File openMapFile (bool *downloaded, const char *file, const char *title)
{
        resetWatchdog();

        // assume no download yet
        *downloaded = false;

        // putting all variables up here avoids pendantic goto warnings
        File f;
        WiFiClient client;
        uint32_t filesize;
        time_t local_time = 0;
        time_t remote_time = 0;
        char hdr_buf[BHDRSZ];
        int nr = 0;
        bool file_ok = false;

        Serial.printf (_FX("%s: %s\n"), title, file);

        // start remote file download, even if only to check whether newer
        if (wifiOk() && client.connect(backend_host, backend_port)) {
            snprintf (hdr_buf, sizeof(hdr_buf), _FX("/maps/%s"), file);
            httpHCGET (client, backend_host, hdr_buf);
            char lm_str[50];
            if (!httpSkipHeader (client, _FX("Last-Modified:"), lm_str, sizeof(lm_str))
                                                || !crackLastModified (lm_str, remote_time)) {
                mapMsg (true, 1000, _FX("%s: network err - try local"), title);
                client.stop();
            }
            Serial.printf (_FX("%s: %ld remote_time\n"), title, (long)remote_time);
        }
        
        // even if no net connection, still try using local file if available

        // open local file
        f = LittleFS.open (file, "r");
        if (!f) {
            mapMsg (false, 1000, _FX("%s: not local"), title);
            goto out;
        }

        // file is "bad" if remote is newer than flash
        local_time = f.getCreationTime();
        Serial.printf (_FX("%s: %ld local_time\n"), title, (long)local_time);
        if (client.connected() && remote_time > local_time) {
            mapMsg (false, 1000, _FX("%s: found newer map"), title);
            goto out;
        }

        // read local file header
        nr = f.read ((uint8_t*)hdr_buf, BHDRSZ);
        if (nr != BHDRSZ) {
            mapMsg (true, 1000, _FX("%s: read err"), title);
            goto out;
        }

        // check flash file type and size
        if (!bmpHdrOk (hdr_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            mapMsg (true, 1000, _FX("%s: bad format"), title);
            goto out;
        }
        if (filesize != f.size()) {
            mapMsg (true, 1000, _FX("%s: wrong size"), title);
            goto out;
        }

        // all good
        file_ok = true;

    out:

        // download if not ok for any reason but remote connection is ok
        if (!file_ok && client.connected()) {

            if (f) {
                // file exists but is not correct in some way
                f.close();
                LittleFS.remove(file);
            }

            // insure room
            cleanFLASH (title, 1);

            // download and open again if success
            if (downloadMapFile (client, file, title)) {
                *downloaded = true;
                f = LittleFS.open (file, "r");
            }
        }

        // finished with remote connection
        client.stop();

        // return result, open if good or closed if not
        return (f);
}

/* install maps that require a fresh query and thus always a fresh download.
 * return whether ok
 */
static bool installQueryMaps (const char *page, const char *style, const float MHz)
{
        resetWatchdog();

        // get clock time
        time_t t = nowWO();
        int yr = year(t);
        int mo = month(t);
        int hr = hour(t);

        // prepare query
        StackMalloc query_mem(300);
        char *query = (char *) query_mem.getMem();
        snprintf (query, query_mem.getSize(),
            _FX("%s?YEAR=%d&MONTH=%d&UTC=%d&TXLAT=%.3f&TXLNG=%.3f&PATH=%d&WATTS=%d&WIDTH=%d&HEIGHT=%d&MHZ=%.2f&TOA=%.1f&MODE=%d&TOA=%.1f"),
            page, yr, mo, hr, de_ll.lat_d, de_ll.lng_d, show_lp, bc_power, HC_MAP_W, HC_MAP_H,
            MHz, bc_toa, bc_modevalue, bc_toa);

        Serial.printf (_FX("%s query: %s\n"), style, query);

        // assign a style and compose names and titles
        char dfile[32];                 // match LFS_NAME_MAX
        char nfile[32];
        char dtitle[NV_COREMAPSTYLE_LEN+10];
        char ntitle[NV_COREMAPSTYLE_LEN+10];
        buildMapNames (style, dfile, nfile, dtitle, ntitle);

        // insure fresh start
        cleanFLASH (dtitle, 2);
        invalidatePixels();

        // download new voacap maps
        updateClocks(false);
        WiFiClient client;
        bool ok = false;
        if (wifiOk() && client.connect(backend_host, backend_port)) {
            httpHCGET (client, backend_host, query);
            ok = httpSkipHeader (client) && downloadMapFile (client, dfile, dtitle)
                                         && downloadMapFile (client, nfile, ntitle);
            client.stop();
        }

        // install if ok
        if (ok) {
            day_file = LittleFS.open (dfile, "r");
            night_file = LittleFS.open (nfile, "r");
            ok = installFilePixels (dfile, nfile);
        }

        if (!ok)
            Serial.printf (_FX("%s: fail\n"), style);

        return (ok);
}

/* install maps for core_map that are just files maintained on the server, no update query required.
 * Download only if absent or newer on server.
 * return whether ok
 */
static bool installFileMaps()
{
        resetWatchdog();

        // confirm core_map is one of the file styles
        if (core_map != CM_COUNTRIES && core_map != CM_TERRAIN && core_map != CM_DRAP
                            && core_map != CM_AURORA && core_map != CM_WX)
            fatalError (_FX("style not a file map %d"), core_map);        // does not return

        // create names and titles
        const char *style = coremap_names[core_map];
        char dfile[LFS_NAME_MAX];
        char nfile[LFS_NAME_MAX];
        char dtitle[NV_COREMAPSTYLE_LEN+10];
        char ntitle[NV_COREMAPSTYLE_LEN+10];
        buildMapNames (style, dfile, nfile, dtitle, ntitle);

        // close any previous
        invalidatePixels();
        if (day_file)
            day_file.close();
        if (night_file)
            night_file.close();

        // open each file, downloading if newer or not found locally
        bool dd = false, nd = false;
        day_file = openMapFile (&dd, dfile, dtitle);
        night_file = openMapFile (&nd, nfile, ntitle);

        // install pixels
        if (installFilePixels (dfile, nfile)) {

            // note whether needed to be downloaded
            if (dd || nd)
                Serial.printf (_FX("%s: fresh download\n"), dtitle);

            // ok!
            return (true);
        } else {
            // phoey!
            return (false);
        }
}

/* retrieve and install new MUF map for the current time.
 * return whether ok
 */
static bool installMUFMaps()
{
        mapMsg (true, 0, _FX("Calculating %s..."), muf_style);
        return (installQueryMaps (_FX("/fetchVOACAP-MUF.pl"), muf_style, 0));
}

/* retrieve and install VOACAP maps for the current time and given band.
 * return whether ok
 */
static bool installPropMaps (void)
{
        char s[NV_COREMAPSTYLE_LEN];
        mapMsg (true, 0, _FX("Calculating %s %s..."), getMapStyle(s), prop_style);
        float MHz = propMap2MHz(prop_map.band);
        if (prop_map.type == PROPTYPE_REL)
            return (installQueryMaps (_FX("/fetchVOACAPArea.pl"), prop_style, MHz));
        else if (prop_map.type == PROPTYPE_TOA)
            return (installQueryMaps (_FX("/fetchVOACAP-TOA.pl"), prop_style, MHz));
        else
            fatalError (_FX("unknow prop map type %d"), prop_map.type);
        return (false);
}

/* install fresh maps depending on prop_map and core_map.
 * return whether ok
 */
bool installFreshMaps()
{
        if (prop_map.active)
            return (installPropMaps());

        bool core_ok = false;
        if (core_map == CM_MUF)
            core_ok = installMUFMaps();
        else
            core_ok = installFileMaps();
        if (core_ok)
            NVWriteString (NV_COREMAPSTYLE, coremap_names[core_map]);
        return (core_ok);
}

/* init core_map from NV, or set a default, and always disable prop_map.
 * return whether ok
 */
void initCoreMaps()
{
        // initially no map is set
        core_map = CM_NONE;
        prop_map.active = false;

        // set core from NV if present and valid
        char s[NV_COREMAPSTYLE_LEN];
        if (NVReadString (NV_COREMAPSTYLE, s)) {
            for (int i = 0; i < CM_N; i++) {
                if (strcmp (coremap_names[i], s) == 0) {
                    core_map = (CoreMaps)i;
                    break;
                }
            }
        }

        // pick default if still not set
        if (core_map == CM_NONE) {
            NVWriteString (NV_COREMAPSTYLE, coremap_names[CM_TERRAIN]);
            core_map = CM_TERRAIN;
        }
}



/* produce a listing of the map storage directory.
 * N.B. return malloced array and malloced name -- caller must free()
 */
FS_Info *getConfigDirInfo (int *n_info, char **fs_name, uint64_t *fs_size, uint64_t *fs_used)
{
        // get basic fs info
        FSInfo fs_info;
        LittleFS.info(fs_info);

        // pass back basic info
        *fs_name = strdup (_FX("HamClock file system"));
        *fs_size = fs_info.totalBytes;
        *fs_used = fs_info.usedBytes;

        // build each entry
        FS_Info *fs_array = NULL;
        int n_fs = 0;
        Dir dir = LittleFS.openDir("/");
        while (dir.next()) {

            // extend array
            fs_array = (FS_Info *) realloc (fs_array, (n_fs+1)*sizeof(FS_Info));
            if (!fs_array)
                fatalError ("alloc dir failed: %d", n_fs);     // no _FX if alloc failing
            FS_Info *fip = &fs_array[n_fs++];

            // store name
            strncpy (fip->name, dir.fileName().c_str(), sizeof(fip->name)-1);
            fip->name[sizeof(fip->name)-1] = 0;

            // store time
            time_t t = dir.fileCreationTime();
            fip->t0 = t;

            // as handy date string too
            int yr = year(t);
            int mo = month(t);
            int dy = day(t);
            int hr = hour(t);
            int mn = minute(t);
            int sc = second(t);
            snprintf (fip->date, sizeof(fip->date), _FX("%04d-%02d-%02dT%02d:%02d:%02dZ"),
                                yr, mo, dy, hr, mn, sc);

            // store length
            fip->len = dir.fileSize();
        }
        // Dir has no close method, hope destructor cleans up

        // nice sorted order
        qsort (fs_array, n_fs, sizeof(FS_Info), FSInfoNameQsort);

        // ok
        *n_info = n_fs;
        return (fs_array);
}

/* return the current map style, meaning core style or short prop map name.
 * N.B. do not use this for setting NV_COREMAPSTYLE
 * N.B. nevertheless s[] is assumed to be at least NV_COREMAPSTYLE_LEN
 */
const char *getMapStyle (char s[])
{
        if (prop_map.active) {
            // +1 to suppress warning that s might overflow since we know the %d lengths here
            snprintf (s, NV_COREMAPSTYLE_LEN+1, "%dm/%s", propMap2Band (prop_map.band),
                                prop_map.type == PROPTYPE_REL ? "REL" : "TOA");
        } else {
            NVReadString (NV_COREMAPSTYLE, s);
        }

        return (s);
}

/* return MHz for the given PropMapSetting.band
 * N.B. match column headings in voacapx.out
 */
float propMap2MHz (PropMapBand band)
{
        switch (band) {
        case PROPBAND_80M: return ( 3.6);
        case PROPBAND_40M: return ( 7.1);
        case PROPBAND_30M: return (10.1);
        case PROPBAND_20M: return (14.1);
        case PROPBAND_17M: return (18.1);
        case PROPBAND_15M: return (21.1);
        case PROPBAND_12M: return (24.9);
        case PROPBAND_10M: return (28.2);
        default: fatalError (_FX("bad MHz PMS %d"), band);
        }

        // lint
        return (0);
}

/* return band for the given PropMapSetting.band
 */
int propMap2Band (PropMapBand band)
{
        switch (band) {
        case PROPBAND_80M: return (80);
        case PROPBAND_40M: return (40);
        case PROPBAND_30M: return (30);
        case PROPBAND_20M: return (20);
        case PROPBAND_17M: return (17);
        case PROPBAND_15M: return (15);
        case PROPBAND_12M: return (12);
        case PROPBAND_10M: return (10);
        default: fatalError (_FX("bad PMS %d"), band);
        }

        // lint
        return (0);
}


/* return whether the map scale is (or should be) visible now
 * N.B. must agree with drawMapScale()
 */
bool mapScaleIsUp(void)
{
    return (prop_map.active
                || core_map == CM_DRAP || core_map == CM_MUF || core_map == CM_AURORA || core_map == CM_WX);
}

/* draw the appropriate scale at mapscale_b depending on core_map or prop_map, if any.
 * N.B. we move mapscale_b depending on rss_on
 */
void drawMapScale()
{
    // color scale. values must be monitonivally increasing.
    typedef struct {
        float value;                                    // world value
        uint32_t color;                                 // 24 bit RGB scale color
        bool black_text;                                // black text, else white
    } MapScalePoint;

    // CM_DRAP and CM_MUF
    static PROGMEM const MapScalePoint d_scale[] = {    // see fetchDRAP.pl and fetchVOACAP-MUF.pl
        {0,  0x000000, 0},
        {4,  0x4E138A, 0},
        {9,  0x001EF5, 0},
        {15, 0x78FBD6, 1},
        {20, 0x78FA4D, 1},
        {27, 0xFEFD54, 1},
        {30, 0xEC6F2D, 1},
        {35, 0xE93323, 1},
    };

    // CM_AURORA
    static PROGMEM const MapScalePoint a_scale[] = {    // see fetchAurora.pl
        {0,   0x282828, 0},
        {25,  0x00FF00, 1},
        {50,  0xFFFF00, 1},
        {75,  0xEA6D2D, 1},
        {100, 0xFF0000, 1},
    };

    // CM_WX
    static PROGMEM const MapScalePoint w_scale[] = {    // see fetchWordWx.pl
        // values are degs C
        {-50,  0xD1E7FF, 1},
        {-40,  0xB5D5FF, 1},
        {-30,  0x88BFFF, 1},
        {-20,  0x73AAFF, 1},
        {-10,  0x4078D9, 0},
        {0,    0x2060A6, 0},
        {10,   0x009EDC, 1},
        {20,   0xBEE5B4, 1},
        {30,   0xFF8C24, 1},
        {40,   0xEE0051, 1},
        {50,   0x5B0023, 1},
    };

    // PROPTYPE_TOA
    static PROGMEM const MapScalePoint t_scale[] = {    // see fetchVOACAP-TOA.pl
        {0,    0x0000F0, 0},
        {6,    0xF0B060, 1},
        {30,   0xF00000, 1},
    };

    // PROPTYPE_REL
    static PROGMEM const MapScalePoint r_scale[] = {    // see fetchVOACAPArea.pl
        {0,    0x666666, 0},
        {21,   0xEE6766, 0},
        {40,   0xEEEE44, 1},
        {60,   0xEEEE44, 1},
        {83,   0x44CC44, 0},
        {100,  0x44CC44, 0},
    };


    // set these depending on map
    const MapScalePoint *msp = NULL;                    // one of above tables
    unsigned n_scale = 0;                               // n entries in table
    unsigned n_labels = 0;                              // n labels in scale
    const char *title = NULL;                           // scale title

    if (prop_map.active) {

        switch (prop_map.type) {
        case PROPTYPE_TOA:
            msp = t_scale;
            n_scale = NARRAY(t_scale);
            n_labels = 7;
            title = "DE TOA, degs";
            break;
        case PROPTYPE_REL:
            msp = r_scale;
            n_scale = NARRAY(r_scale);
            n_labels = 6;
            title = "% Reliability";
            break;
        }

    } else {

        switch (core_map) {
        case CM_MUF:        // fallthru
        case CM_DRAP:
            msp = d_scale;
            n_scale = NARRAY(d_scale);
            n_labels = 8;
            title = "MHz";
            break;
        case CM_AURORA:
            msp = a_scale;
            n_scale = NARRAY(a_scale);
            n_labels = 11;
            title = "% Chance";
            break;
        case CM_WX:
            msp = w_scale;
            n_scale = NARRAY(w_scale);
            n_labels = useMetricUnits() ? 11 : 10;
            title = "Degs C";
            break;
        case CM_COUNTRIES:
        case CM_TERRAIN:
        case CM_N:                                      // lint
            // no scale
            return;
        }

    }

    resetWatchdog();

    // handy accessors for ESP
    #define _MS_PTV(i) pgm_read_float(&msp[i].value)            // handy access to msp[i].value
    #define _MS_PTC(i) pgm_read_dword(&msp[i].color)            // handy access to msp[i].color
    #define _MS_PTB(i) pgm_read_byte(&msp[i].black_text)        // handy access to msp[i].black_text

    // geometry setup
    #define _MS_X0     mapscale_b.x                             // left x
    #define _MS_X1     (mapscale_b.x + mapscale_b.w)            // right x
    #define _MS_DX     (_MS_X1-_MS_X0)                          // width
    #define _MS_MINV   _MS_PTV(0)                               // min value
    #define _MS_MAXV   _MS_PTV(n_scale-1)                       // max value
    #define _MS_DV     (_MS_MAXV-_MS_MINV)                      // value span
    #define _MS_V2X(v) (_MS_X0 + _MS_DX*((v)-_MS_MINV)/_MS_DV)  // convert value to x
    #define _MS_PRY    (mapscale_b.y+1U)                        // text y

    // set mapscale_b.y above RSS if on else at the bottom
    mapscale_b.y = rss_on ? rss_bnr_b.y - mapscale_b.h: map_b.y + map_b.h - mapscale_b.h;

    // draw smoothly-interpolated color scale
    for (unsigned i = 1; i < n_scale; i++) {
        uint8_t dm = _MS_PTV(i) - _MS_PTV(i-1);
        uint8_t r0 = _MS_PTC(i-1) >> 16;
        uint8_t g0 = (_MS_PTC(i-1) >> 8) & 0xFF;
        uint8_t b0 = _MS_PTC(i-1) & 0xFF;
        uint8_t r1 = _MS_PTC(i) >> 16;
        uint8_t g1 = (_MS_PTC(i) >> 8) & 0xFF;
        uint8_t b1 = _MS_PTC(i) & 0xFF;
        for (uint16_t x = _MS_V2X(_MS_PTV(i-1)); x <= _MS_V2X(_MS_PTV(i)); x++) {
            if (x < mapscale_b.x + mapscale_b.w) {              // the _MS macros can overflow slightlty
                float value = _MS_MINV + (float)_MS_DV*(x - _MS_X0)/_MS_DX;
                float frac = CLAMPF ((value - _MS_PTV(i-1))/dm,0,1);
                uint16_t new_c = RGB565(r0+frac*(r1-r0), g0+frac*(g1-g0), b0+frac*(b1-b0));
                tft.drawLine (x, mapscale_b.y, x, mapscale_b.y+mapscale_b.h-1, 1, new_c);
            }
        }
    }

    // determine DRAP marker location, if used
    uint16_t drap_x = 0;
    if (core_map == CM_DRAP) {
        SPWxValue ssn, flux, kp, swind, drap, bz, bt;
        NOAASpaceWx noaaspw;
        float path[BMTRX_COLS];
        char xray[10];
        time_t noaaspw_age, xray_age, path_age;
        getSpaceWeather (ssn, flux, kp, swind, drap, bz, bt, noaaspw,noaaspw_age,xray,xray_age,path,path_age);
        if (drap.age < 1800 && drap.value != SPW_ERR) {
            // find drap marker but beware range overflow and leave room for full width
            float v = CLAMPF (drap.value, _MS_MINV, _MS_MAXV);
            drap_x = CLAMPF (_MS_V2X(v), mapscale_b.x+3, mapscale_b.x + mapscale_b.w - 4);
        }
    }

    // draw labels inside mapscale_b but may need to build F scale for WX

    // use labels directly unless need to create F weather scale
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    StackMalloc ticks_v_mem((n_labels+2)*sizeof(float));
    StackMalloc ticks_x_mem((n_labels+2)*sizeof(uint16_t));
    float *ticks_v = (float *) ticks_v_mem.getMem();
    uint16_t *ticks_x = (uint16_t *) ticks_x_mem.getMem();
    int n_ticks;
    const char *my_title;

    // prep values and center x locations
    if (core_map == CM_WX && !useMetricUnits()) {

        // switch to F scale
        my_title = "Degs F";
        n_ticks = tickmarks (CEN2FAH(_MS_MINV), CEN2FAH(_MS_MAXV), n_labels, ticks_v);
        for (int i = 0; i < n_ticks; i++) {
            float value = roundf(ticks_v[i]);                   // value printed is F ...
            float value_c = FAH2CEN(value);                     // ... but position is based on C
            ticks_v[i] = value;
            ticks_x[i] = _MS_V2X(value_c);
        }

    } else {

        // generate evenly-spaced labels from min to max, inclusive
        my_title = title;
        n_ticks = n_labels;
        for (unsigned i = 0; i < n_labels; i++) {
            ticks_v[i] = _MS_MINV + _MS_DV*i/(n_labels-1);
            ticks_x[i] = _MS_V2X(ticks_v[i]);
        }

    }

    // print tick marks across mapscale_b but avoid drap marker
    for (int i = 1; i < n_ticks; i++) {                         // skip first for title

        // skip if off scale or near drap
        const uint16_t ti_x = ticks_x[i];
        if (ti_x < _MS_X0 || ti_x > _MS_X1 || (drap_x && ti_x >= drap_x - 15 && ti_x <= drap_x + 15))
            continue;

        // center but beware edges (we already skipped first so left edge is never a problem)
        char buf[20];
        snprintf (buf, sizeof(buf), "%.0f", ticks_v[i]);
        uint16_t buf_w = getTextWidth(buf);
        uint16_t buf_lx = ti_x - buf_w/2;
        uint16_t buf_rx = buf_lx + buf_w;
        if (buf_rx > _MS_X1 - 2) {
            buf_rx = _MS_X1 - 2;
            buf_lx = buf_rx - buf_w;
        }
        tft.setCursor (buf_lx, _MS_PRY);
        tft.setTextColor (_MS_PTB(i*n_scale/n_ticks) ? RA8875_BLACK : RA8875_WHITE);
        tft.print (buf);
    }

    // draw scale meaning
    tft.setTextColor (_MS_PTB(0) ? RA8875_BLACK : RA8875_WHITE);
    tft.setCursor (_MS_X0 + 4, _MS_PRY);
    tft.print (my_title);

    // if DRAP mark the max freq
    if (drap_x) {
        // use lines for a perfect vetical match to scale
        tft.drawLine (drap_x-2, mapscale_b.y, drap_x-2, mapscale_b.y+mapscale_b.h-1, 1, RA8875_BLACK);
        tft.drawLine (drap_x-1, mapscale_b.y, drap_x-1, mapscale_b.y+mapscale_b.h-1, 1, RA8875_RED);
        tft.drawLine (drap_x,   mapscale_b.y, drap_x,   mapscale_b.y+mapscale_b.h-1, 1, RA8875_RED);
        tft.drawLine (drap_x+1, mapscale_b.y, drap_x+1, mapscale_b.y+mapscale_b.h-1, 1, RA8875_RED);
        tft.drawLine (drap_x+2, mapscale_b.y, drap_x+2, mapscale_b.y+mapscale_b.h-1, 1, RA8875_BLACK);
    }

}


/* erase mapscale_b by redrawing map within
 * N.B. beware globals being temporarily changed -- see comments
 */
void eraseMapScale ()
{
    resetWatchdog();

    // save then move mapscale_b off the map so drawMapCoord doesn't skip it
    SBox db = mapscale_b;
    mapscale_b.y = 0;

    // save whether rss is on too because it is skipped also
    uint8_t rs = rss_on;
    rss_on = false;

    // only mercator can erase by just redrawing map
    if (map_proj != MAPP_MERCATOR)
        fillSBox (db, RA8875_BLACK);

    // restore map
    for (uint16_t y = db.y; y < db.y+db.h; y++) {
        for (uint16_t x = db.x; x < db.x+db.w; x++)
            drawMapCoord (x, y);
        drawSatPointsOnRow (y);
    }

    // restore
    mapscale_b = db;
    rss_on = rs;
}

/* log and show message over map_b.
 * always show if ESP else only if force.
 */
void mapMsg (bool force, uint32_t dwell_ms, const char *fmt, ...)
{
#if defined(_IS_ESP8266)
    (void)force;        // lint
    bool doit = true;
#else
    bool doit = force;
#endif

    if (doit) {
        // format msg
        va_list ap;
        va_start(ap, fmt);
        char msg[200];
        vsnprintf (msg, sizeof(msg), fmt, ap);
        va_end(ap);

        // log
        Serial.println (msg);

        // show over map
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RA8875_WHITE);
        size_t msg_l = getTextWidth(msg);
        tft.fillRect (map_b.x + map_b.w/5, map_b.y+map_b.h/3, 3*map_b.w/5, 40, RA8875_BLACK);
        tft.setCursor (map_b.x + (map_b.w-msg_l)/2, map_b.y+map_b.h/3+30);
        tft.print(msg);
        tft.drawPR();

        // dwell
        wdDelay(dwell_ms);
    }
}
