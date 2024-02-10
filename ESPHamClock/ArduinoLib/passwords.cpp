/* very simple system to capture passwords for different categories.
 * THIS IS HARDLY SECURE, JUST A LITTLE BETTER THAN NOTHING.
 */

#include "Arduino.h"


/* store category and hashed password pairs
 */

typedef uint32_t crc_t;

typedef struct {
    const char *cat;                                    // malloced category
    crc_t hash;                                         // pw_hash (password)
} Password;

static Password *passwords;                             // malloced list
static int n_passwords;                                 // n in list


/* generated with: pycrc --model=crc-32 --algorithm=bbf --generate c --xor-in=0
 */
static crc_t crc_update(crc_t crc, const void *data, size_t data_len)
{
    const unsigned char *d = (const unsigned char *)data;
    unsigned int i;
    crc_t bit;
    unsigned char c;

    while (data_len--) {
        c = *d++;
        for (i = 0x01; i & 0xff; i <<= 1) {
            bit = (crc & 0x80000000) ^ ((c & i) ? 0x80000000 : 0);
            crc <<= 1;
            if (bit) {
                crc ^= 0x04c11db7;
            }
        }
        crc &= 0xffffffff;
    }
    return crc & 0xffffffff;
}

/* return hash of the given password string.
 */
static crc_t pw_hash (const char *pw)
{
    return (crc_update (0, pw, strlen(pw)));
}

/* capture the categories and passwords in the given file into passwords[].
 * exit if trouble.
 */
void capturePasswords (const char *fn)
{
        // open file
        FILE *fp = fopen (fn, "r");
        if (!fp) {
            fprintf (stderr, "%s: %s\n", fn, strerror(errno));
            exit(1);
        }

        // crack each line into passwords[]
        char buf[100];
        int line_n = 0;
        while (fgets (buf, sizeof(buf), fp)) {

            // another
            line_n++;

            // skip comments
            if (buf[0] == '#')
                continue;

            // skip leading white to find category
            char *cat = buf;
            while (isspace(*cat))
                cat++;

            // skip comments and blank lines
            if (*cat == '#' || *cat == '\0')
                continue;

            // category continues to first whitespace, which becomes start of password
            char *pw = cat;
            while (!isspace(*pw)) {
                if (*pw == '\0') {
                    fprintf (stderr, "%s: no category on line %d\n", fn, line_n);
                    exit(1);
                }
                pw++;
            }

            // terminate category
            *pw++ = '\0';

            // skip leading white before pw
            while (isspace (*pw))
                pw++;

            // work back from end removing white
            char *pw_end;
            for (pw_end = pw + strlen(pw); --pw_end > pw; ) {
                if (isspace(*pw_end))
                    *pw_end = '\0';
                else
                    break;
            }
            if (pw_end <= pw) {
                fprintf (stderr, "%s: no password on line %d\n", fn, line_n);
                exit(1);
            }

            // add to list
            printf ("%s: '%s' '%s'\n", fn, cat, pw);
            passwords = (Password *) realloc (passwords, (n_passwords+1) * sizeof(Password));
            Password &new_pw = passwords[n_passwords++];
            new_pw.cat = strdup (cat);
            new_pw.hash = pw_hash (pw);
            
            // erase from memory asap
            memset (buf, 10, sizeof(buf));
        }
        fclose (fp);
}

/* return whether the proposed password for the given category is correct.
 * if candidate is NULL then just return whether category has been set.
 * if both are NULL then just return whether -p was invoked at all.
 */
bool testPassword (const char *category, const char *candidate_pw)
{
        if (!category && !candidate_pw)
            return (n_passwords > 0);

        crc_t cand_hash = candidate_pw ? pw_hash (candidate_pw) : 0;

        for (int i = 0; i < n_passwords; i++) {
            Password &pw = passwords[i];
            if (strcmp (category, pw.cat) == 0 && (!candidate_pw || pw.hash == cand_hash))
                return (true);
        }

        return (false);
}
