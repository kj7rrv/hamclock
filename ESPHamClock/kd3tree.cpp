/* kd tree specifically for fast nearest-neighbor of lat and long.
 *
 * inspired by https://rosettacode.org/wiki/K-d_tree
 *
 * usage: call mkKD3NodeTree() once then nearestKD3Node() for each lookup; see unit test for usage.
 *
 * to build and run a stand-alone main test:
 *    g++ -Wall -O2 -D_UNIT_TEST -o x.kd3tree kd3tree.cpp && ./x.kd3tree 
 */


/* use HamClock.h but if unit test then define here what we need from it
 */

#if defined (_UNIT_TEST)


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>


#define _IS_UNIX

typedef struct {
    float lat, lng;                     // radians north, east
    float lat_d, lng_d;                 // degrees +N +E
} LatLong;


#define M_PIF   3.14159265F
#define ERAD_M  3959.0F                 // earth radius, miles


 
#define deg2rad(d)      ((M_PIF/180)*(d))
#define rad2deg(d)      ((180/M_PIF)*(d))

struct kd_node_t {
    float s[3];                         // xyz coords on unit sphere
    struct kd_node_t *left, *right;     // branches
    void *data;                         // user data
};
 
typedef struct kd_node_t KD3Node;


#else // !_UNIT_TEST

#include "HamClock.h"

#endif // !_UNIT_TEST




#if defined(_IS_UNIX)

// handy
static float sqr(float a) { return (a*a); }


/* return distance metric between two nodes
 */
static float dist (KD3Node *a, KD3Node *b)
{
    float d2 = sqr(a->s[0] - b->s[0]);
    d2 += sqr(a->s[1] - b->s[1]);
    d2 += sqr(a->s[2] - b->s[2]);
    return (d2);
}

static void swap(KD3Node *x, KD3Node *y)
{
    KD3Node tmp = *x;
    *x = *y;
    *y = tmp;
}

static KD3Node* find_median(KD3Node *start, KD3Node *end, int idx)
{
    if (end <= start) return NULL;
    if (end == start + 1)
        return start;

    KD3Node *p, *store, *md = start + (end - start) / 2;

    float pivot;
    while (1) {
        pivot = md->s[idx];
 
        swap(md, end - 1);
        for (store = p = start; p < end; p++) {
            if (p->s[idx] < pivot) {
                if (p != store)
                    swap(p, store);
                store++;
            }
        }
        swap(store, end - 1);
 
        /* median has duplicate values */
        if (store->s[idx] == md->s[idx])
            return md;
 
        if (store > md) end = store;
        else        start = store;
    }
}
 
/* transform and array of KD3Node into a proper kd3tree in place.
 * initial call with idx 0.
 */
KD3Node* mkKD3NodeTree (KD3Node *t, int len, int idx)
{
    KD3Node *n;
 
    if (!len) return NULL;
 
    if ((n = find_median(t, t + len, idx))) {
        idx = (idx + 1) % 3;
        n->left  = mkKD3NodeTree(t, n - t, idx);
        n->right = mkKD3NodeTree(n + 1, t + len - (n + 1), idx);
    }
    return n;
}
 
/* given a kd3tree created with mkKD3NodeTree, find the closest entry to nd.
 * initial call with idx 0.
 */
void nearestKD3Node (KD3Node *root, KD3Node *nd, int idx, KD3Node **best, float *best_dist,
    int *n_visited)
{
    float d, dx, dx2;
    
    if (!root) return;
    d = dist(root, nd);
    dx = root->s[idx] - nd->s[idx];
    dx2 = dx * dx;
 
    (*n_visited)++;
 
    if (!*best || d < *best_dist) {
        *best_dist = d;
        *best = root;
    }

    idx = (idx + 1) % 3;
 
    nearestKD3Node(dx > 0 ? root->left : root->right, nd, idx, best, best_dist, n_visited);
    if (dx2 >= *best_dist) return;
    nearestKD3Node(dx > 0 ? root->right : root->left, nd, idx, best, best_dist, n_visited);
}

/* handy convert ll.lat/lng to KD3Node
 */
void ll2KD3Node (const LatLong &ll, KD3Node &n)
{
    float clat = cosf(ll.lat);
    n.s[0] = clat*cosf(ll.lng);
    n.s[1] = clat*sinf(ll.lng);
    n.s[2] = sinf(ll.lat);
}

/* handy convert KD3Node to ll
 */
void KD3Node2ll (const KD3Node &n, LatLong &ll)
{
    ll.lat = asinf (n.s[2]);
    ll.lat_d = rad2deg(ll.lat);

    ll.lng = atan2f (n.s[1], n.s[0]);
    ll.lng_d = rad2deg(ll.lng);
}

/* handy convert nearestKD3Node() best_dist to earth distance in miles
 * this is actually the chordal distance, close enough especially for small distances.
 */
float nearestKD3Dist2Miles(float d)
{
    return (ERAD_M*sqrtf(d));
}

#endif // _IS_UNIX

 


#if defined (_UNIT_TEST)


// N nodes in test tree
#define N 1000000

#define rand1() (rand() / (float)RAND_MAX)

void rand_pt (KD3Node &n)
{
    LatLong ll;
    ll.lat = M_PIF*rand1() - M_PIF/2;
    ll.lng = 2*M_PIF*rand1() - M_PIF;
    ll2KD3Node (ll, n);
}


int main(int ac, char *av[])
{
    int i;
    KD3Node testNode;
    KD3Node *root, *found, *million;
    float best_dist;
    int visited;
 
    million = (KD3Node*) calloc(N, sizeof(KD3Node));

    srand(time(NULL));
    for (i = 0; i < N; i++) {
        rand_pt (million[i]);
        char buf[100];
        sprintf (buf, "%6d %g %g %g", i, million[i].s[0], million[i].s[1], million[i].s[2]);
        million[i].data = strdup(buf);
    }
 
    root = mkKD3NodeTree(million, N, 0);

    rand_pt(testNode);
    visited = 0;
    found = NULL;
    best_dist = 0;
    visited = 0;
    nearestKD3Node(root, &testNode, 0, &found, &best_dist, &visited);
 
    printf(">> Million tree once\nsearching for (%g, %g, %g)\n"
            "found (%g, %g, %g) '%s' dist %g\nvisited %d nodes\n",
            testNode.s[0], testNode.s[1], testNode.s[2],
            found->s[0], found->s[1], found->s[2], (char*)found->data,
            sqrtf(best_dist), visited);
 
    /* search many random points in million tree to see average behavior.
     */

    int seen = 0, test_runs = 10000;
    struct timeval tv0, tv1;
    float worst_dist = 0;
    gettimeofday(&tv0, NULL);
    for (i = 0; i < test_runs; i++) {
        rand_pt(testNode);
        found = NULL;
        best_dist = 0;
        nearestKD3Node(root, &testNode, 0, &found, &best_dist, &seen);
        if (best_dist > worst_dist)
            worst_dist = best_dist;
    }
    gettimeofday(&tv1, NULL);
    printf(">> Million tree %d times\n"
            "visited %d nodes for %d random findings (%f per lookup)\n"
            "worst dist %g\n",
            test_runs, seen, test_runs, seen/(float)test_runs, sqrtf(worst_dist));
    printf ("time %ld us\n", (tv1.tv_sec-tv0.tv_sec)*1000000 + (tv1.tv_usec-tv0.tv_usec));
 
    free(million);
 
    return 0;
}
 
 #endif // _UNIT_TEST
