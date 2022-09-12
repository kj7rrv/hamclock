#include "HamClock.h"


/* solve a spherical triangle:
 *           A
 *          /  \
 *         /    \
 *      c /      \ b
 *       /        \
 *      /          \
 *    B ____________ C
 *           a
 *
 * given A, b, c find B and a in range -PI..B..PI and 0..a..PI, respectively..
 * cap and Bp may be NULL if not interested in either one.
 * N.B. we pass in cos(c) and sin(c) because in many problems one of the sides
 *   remains constant for many values of A and b.
 */
void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp)
{
        float cb = cosf(b);
        float sb = sinf(b);
        float cA = cosf(A);
        float ca = fminf (fmaxf (cb*cc + sb*sc*cA, -1), 1);

        if (cap)
            *cap = ca;

        if (Bp) {
            float sA = sinf(A);
            float y = sA*sb*sc;
            float x = cb - ca*cc;
            *Bp = atan2f (y,x);
        }
}

