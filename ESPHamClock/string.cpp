/* misc string utils
 */

#include "HamClock.h"


/* string hash, commonly referred to as djb2
 * see eg http://www.cse.yorku.ca/~oz/hash.html
 */
uint32_t stringHash (const char *str)
{
    uint32_t hash = 5381;
    int c;

    while ((c = *str++) != '\0')
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

/* convert any upper case letter in str to lower case IN PLACE
 */
void strtolower (char *str)
{
    for (char c = *str; c != '\0'; c = *++str)
        if (isupper(c))
            *str = tolower(c);
}

/* convert any lower case letter in str to upper case IN PLACE
 */
void strtoupper (char *str)
{
    for (char c = *str; c != '\0'; c = *++str)
        if (islower(c))
            *str = toupper(c);
}

