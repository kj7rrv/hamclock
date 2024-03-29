/* inline World Magnetic Model.
 * https://www.ngdc.noaa.gov/geomag/WMM/DoDWMM.shtml
 * 
 * Unit test:
 *    g++ -O2 -Wall -D_TEST_MAIN -o magdecl magdecl.cpp
 */

#if defined(_TEST_MAIN)
#define PROGMEM
#define pgm_read_float(x)  *x
#else
#include "HamClock.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// wmm.cof starting year
static float epoc = 2020.0;

// wmm.cof as two arrays
static const PROGMEM float c0[13][13] = {
    {      0.0, -29404.5,  -2500.0,   1363.9,    903.1,   -234.4,     65.9,     80.6,     23.6,      5.0,     -1.9,      3.0,     -2.0, },
    {   4652.9,  -1450.7,   2982.0,  -2381.0,    809.4,    363.1,     65.6,    -76.8,      9.8,      8.2,     -6.2,     -1.4,     -0.1, },
    {  -2991.6,   -734.8,   1676.8,   1236.2,     86.2,    187.8,     73.0,     -8.3,    -17.5,      2.9,     -0.1,     -2.5,      0.5, },
    {    -82.2,    241.8,   -542.9,    525.7,   -309.4,   -140.7,   -121.5,     56.5,     -0.4,     -1.4,      1.7,      2.4,      1.3, },
    {    282.0,   -158.4,    199.8,   -350.1,     47.9,   -151.2,    -36.2,     15.8,    -21.1,     -1.1,     -0.9,     -0.9,     -1.2, },
    {     47.7,    208.4,   -121.3,     32.2,     99.1,     13.7,     13.5,      6.4,     15.3,    -13.3,      0.6,      0.3,      0.7, },
    {    -19.1,     25.0,     52.7,    -64.4,      9.0,     68.1,    -64.7,     -7.2,     13.7,      1.1,     -0.9,     -0.7,      0.3, },
    {    -51.4,    -16.8,      2.3,     23.5,     -2.2,    -27.2,     -1.9,      9.8,    -16.5,      8.9,      1.9,     -0.1,      0.5, },
    {      8.4,    -15.3,     12.8,    -11.8,     14.9,      3.6,     -6.9,      2.8,     -0.3,     -9.3,      1.4,      1.4,     -0.2, },
    {    -23.3,     11.1,      9.8,     -5.1,     -6.2,      7.8,      0.4,     -1.5,      9.7,    -11.9,     -2.4,     -0.6,     -0.5, },
    {      3.4,     -0.2,      3.5,      4.8,     -8.6,     -0.1,     -4.2,     -3.4,     -0.1,     -8.8,     -3.9,      0.2,      0.1, },
    {     -0.0,      2.6,     -0.5,     -0.4,      0.6,     -0.2,     -1.7,     -1.6,     -3.0,     -2.0,     -2.6,      3.1,     -1.1, },
    {     -1.2,      0.5,      1.3,     -1.8,      0.1,      0.7,     -0.1,      0.6,      0.2,     -0.9,     -0.0,      0.5,     -0.3, },
};
static const PROGMEM float cd0[13][13] = {
    {      0.0,      6.7,    -11.5,      2.8,     -1.1,     -0.3,     -0.6,     -0.1,     -0.1,     -0.1,      0.0,     -0.0,      0.0, },
    {    -25.1,      7.7,     -7.1,     -6.2,     -1.6,      0.6,     -0.4,     -0.3,      0.1,     -0.2,     -0.0,     -0.1,     -0.0, },
    {    -30.2,    -23.9,     -2.2,      3.4,     -6.0,     -0.7,      0.5,     -0.1,     -0.1,     -0.0,     -0.0,     -0.0,     -0.0, },
    {      5.7,     -1.0,      1.1,    -12.2,      5.4,      0.1,      1.4,      0.7,      0.5,      0.4,      0.2,      0.0,      0.0, },
    {      0.2,      6.9,      3.7,     -5.6,     -5.5,      1.2,     -1.4,      0.2,     -0.1,     -0.3,     -0.1,     -0.0,     -0.0, },
    {      0.1,      2.5,     -0.9,      3.0,      0.5,      1.0,     -0.0,     -0.5,      0.4,     -0.0,     -0.2,     -0.1,     -0.0, },
    {      0.1,     -1.8,     -1.4,      0.9,      0.1,      1.0,      0.8,     -0.8,      0.5,      0.3,     -0.0,      0.0,      0.0, },
    {      0.5,      0.6,     -0.7,     -0.2,     -1.2,      0.2,      0.3,      1.0,      0.0,     -0.0,     -0.1,     -0.0,     -0.0, },
    {     -0.3,      0.7,     -0.2,      0.5,     -0.3,     -0.5,      0.4,      0.1,      0.4,     -0.0,     -0.2,     -0.1,      0.0, },
    {     -0.3,      0.2,     -0.4,      0.4,      0.1,     -0.0,     -0.2,      0.5,      0.2,     -0.4,     -0.1,     -0.1,     -0.0, },
    {     -0.0,      0.1,     -0.3,      0.1,     -0.2,      0.1,     -0.0,     -0.1,      0.2,     -0.0,     -0.0,     -0.1,     -0.0, },
    {     -0.0,      0.1,      0.0,      0.2,     -0.0,      0.0,      0.1,     -0.0,     -0.1,      0.0,     -0.0,     -0.1,     -0.0, },
    {     -0.0,      0.0,     -0.1,      0.1,     -0.0,      0.0,     -0.0,      0.1,     -0.0,     -0.0,      0.0,     -0.1,     -0.1, },
};

/* return pointer to a malloced 13x13 2d array of float such that caller can use array[i][j]
 */
static float **malloc1313(void)
{
     #define SZ 13

     // room for SZ row pointers plus SZ*SZ floats
     float **arr = (float **) malloc (sizeof(float *) * SZ + sizeof(float) * SZ * SZ);

     // ptr points to first float
     float *ptr = (float *)(arr + SZ);

     // set up row pointers
     for (int i = 0; i < SZ; i++) 
         arr[i] = (ptr + SZ * i);

     return (arr);
}

static int E0000(int *maxdeg, float alt,
float glat, float glon, float t, float *dec, float *mdp, float *ti,
float *gv)
{
      int maxord,n,m,j,D1,D2,D3,D4;
 
      // N.B. not enough stack space in ESP8266 for these
      // float c[13][13],cd[13][13],tc[13][13],dp[13][13];
      // float snorm[169]
      float **c = malloc1313();
      float **cd = malloc1313();
      float **tc = malloc1313();
      float **dp = malloc1313();
      float **k = malloc1313();
      float *snorm = (float *) malloc (169 * sizeof(float));
 
      float sp[13],cp[13],fn[13],fm[13],pp[13],dtr,a,b,re,
          a2,b2,c2,a4,b4,c4,flnmj,
          dt,rlon,rlat,srlon,srlat,crlon,crlat,srlat2,
          crlat2,q,q1,q2,ct,st,r2,r,d,ca,sa,aor,ar,br,bt,bp,bpp,
          par,temp1,temp2,parp,bx,by,bz,bh;
      float *p = snorm;


// GEOMAG:

/* INITIALIZE CONSTANTS */
      maxord = *maxdeg;
      sp[0] = 0.0;
      cp[0] = *p = pp[0] = 1.0;
      dp[0][0] = 0.0;
      a = 6378.137;
      b = 6356.7523142;
      re = 6371.2;
      a2 = a*a;
      b2 = b*b;
      c2 = a2-b2;
      a4 = a2*a2;
      b4 = b2*b2;
      c4 = a4 - b4;

      // N.B. the algorithm modifies c[][] and cd[][] IN PLACE so they must be inited each time upon entry.
      for (int i = 0; i < 13; i++) {
          for (int j = 0; j < 13; j++) {
              c[i][j] = pgm_read_float (&c0[i][j]);
              cd[i][j] = pgm_read_float (&cd0[i][j]);
          }
      }

/* CONVERT SCHMIDT NORMALIZED GAUSS COEFFICIENTS TO UNNORMALIZED */
      *snorm = 1.0;
      for (n=1; n<=maxord; n++)
      {
        *(snorm+n) = *(snorm+n-1)*(float)(2*n-1)/(float)n;
        j = 2;
        for (m=0,D1=1,D2=(n-m+D1)/D1; D2>0; D2--,m+=D1)
        {
          k[m][n] = (float)(((n-1)*(n-1))-(m*m))/(float)((2*n-1)*(2*n-3));
          if (m > 0)
          {
            flnmj = (float)((n-m+1)*j)/(float)(n+m);
            *(snorm+n+m*13) = *(snorm+n+(m-1)*13)*sqrtf(flnmj);
            j = 1;
            c[n][m-1] = *(snorm+n+m*13)*c[n][m-1];
            cd[n][m-1] = *(snorm+n+m*13)*cd[n][m-1];
          }
          c[m][n] = *(snorm+n+m*13)*c[m][n];
          cd[m][n] = *(snorm+n+m*13)*cd[m][n];
        }
        fn[n] = (float)(n+1);
        fm[n] = (float)n;
      }
      k[1][1] = 0.0;

/*************************************************************************/

// GEOMG1:

      dt = t - epoc;
      if (dt < 0.0 || dt > 5.0) {
          *ti = epoc;                   /* pass back base time for diag msg */
          free(c);
          free(cd);
          free(tc);
          free(dp);
          free(k);
          free(snorm);
          return (-1);
      }

      dtr = M_PI/180.0;
      rlon = glon*dtr;
      rlat = glat*dtr;
      srlon = sinf(rlon);
      srlat = sinf(rlat);
      crlon = cosf(rlon);
      crlat = cosf(rlat);
      srlat2 = srlat*srlat;
      crlat2 = crlat*crlat;
      sp[1] = srlon;
      cp[1] = crlon;

/* CONVERT FROM GEODETIC COORDS. TO SPHERICAL COORDS. */
      q = sqrtf(a2-c2*srlat2);
      q1 = alt*q;
      q2 = ((q1+a2)/(q1+b2))*((q1+a2)/(q1+b2));
      ct = srlat/sqrtf(q2*crlat2+srlat2);
      st = sqrtf(1.0-(ct*ct));
      r2 = (alt*alt)+2.0*q1+(a4-c4*srlat2)/(q*q);
      r = sqrtf(r2);
      d = sqrtf(a2*crlat2+b2*srlat2);
      ca = (alt+d)/r;
      sa = c2*crlat*srlat/(r*d);
      for (m=2; m<=maxord; m++)
      {
        sp[m] = sp[1]*cp[m-1]+cp[1]*sp[m-1];
        cp[m] = cp[1]*cp[m-1]-sp[1]*sp[m-1];
      }
      aor = re/r;
      ar = aor*aor;
      br = bt = bp = bpp = 0.0;
      for (n=1; n<=maxord; n++)
      {
        ar = ar*aor;
        for (m=0,D3=1,D4=(n+m+D3)/D3; D4>0; D4--,m+=D3)
        {
/*
   COMPUTE UNNORMALIZED ASSOCIATED LEGENDRE POLYNOMIALS
   AND DERIVATIVES VIA RECURSION RELATIONS
*/
          if (n == m)
          {
            *(p+n+m*13) = st**(p+n-1+(m-1)*13);
            dp[m][n] = st*dp[m-1][n-1]+ct**(p+n-1+(m-1)*13);
            goto S50;
          }
          if (n == 1 && m == 0)
          {
            *(p+n+m*13) = ct**(p+n-1+m*13);
            dp[m][n] = ct*dp[m][n-1]-st**(p+n-1+m*13);
            goto S50;
          }
          if (n > 1 && n != m)
          {
            if (m > n-2) *(p+n-2+m*13) = 0.0;
            if (m > n-2) dp[m][n-2] = 0.0;
            *(p+n+m*13) = ct**(p+n-1+m*13)-k[m][n]**(p+n-2+m*13);
            dp[m][n] = ct*dp[m][n-1] - st**(p+n-1+m*13)-k[m][n]*dp[m][n-2];
          }
S50:
/*
    TIME ADJUST THE GAUSS COEFFICIENTS
*/
          tc[m][n] = c[m][n]+dt*cd[m][n];
          if (m != 0) tc[n][m-1] = c[n][m-1]+dt*cd[n][m-1];
/*
    ACCUMULATE TERMS OF THE SPHERICAL HARMONIC EXPANSIONS
*/
          par = ar**(p+n+m*13);
          if (m == 0)
          {
            temp1 = tc[m][n]*cp[m];
            temp2 = tc[m][n]*sp[m];
          }
          else
          {
            temp1 = tc[m][n]*cp[m]+tc[n][m-1]*sp[m];
            temp2 = tc[m][n]*sp[m]-tc[n][m-1]*cp[m];
          }
          bt = bt-ar*temp1*dp[m][n];
          bp += (fm[m]*temp2*par);
          br += (fn[n]*temp1*par);
/*
    SPECIAL CASE:  NORTH/SOUTH GEOGRAPHIC POLES
*/
          if (st == 0.0 && m == 1)
          {
            if (n == 1) pp[n] = pp[n-1];
            else pp[n] = ct*pp[n-1]-k[m][n]*pp[n-2];
            parp = ar*pp[n];
            bpp += (fm[m]*temp2*parp);
          }
        }
      }
      if (st == 0.0) bp = bpp;
      else bp /= st;
/*
    ROTATE MAGNETIC VECTOR COMPONENTS FROM SPHERICAL TO
    GEODETIC COORDINATES
*/
      bx = -bt*ca-br*sa;
      by = bp;
      bz = bt*sa-br*ca;
/*
    COMPUTE DECLINATION (DEC), INCLINATION (DIP) AND
    TOTAL INTENSITY (TI)
*/
      bh = sqrtf((bx*bx)+(by*by));
      *ti = sqrtf((bh*bh)+(bz*bz));
      *dec = atan2f(by,bx)/dtr;
      *mdp = atan2f(bz,bh)/dtr;
/*
    COMPUTE MAGNETIC GRID VARIATION IF THE CURRENT
    GEODETIC POSITION IS IN THE ARCTIC OR ANTARCTIC
    (I.E. GLAT > +55 DEGREES OR GLAT < -55 DEGREES)

    OTHERWISE, SET MAGNETIC GRID VARIATION TO -999.0
*/
      *gv = -999.0;
      if (fabs(glat) >= 55.)
      {
        if (glat > 0.0 && glon >= 0.0) *gv = *dec-glon;
        if (glat > 0.0 && glon < 0.0) *gv = *dec+fabs(glon);
        if (glat < 0.0 && glon >= 0.0) *gv = *dec+glon;
        if (glat < 0.0 && glon < 0.0) *gv = *dec-fabs(glon);
        if (*gv > +180.0) *gv -= 360.0;
        if (*gv < -180.0) *gv += 360.0;
      }

     free(c);
     free(cd);
     free(tc);
     free(dp);
     free(k);
     free(snorm);

     return (0);
}


/* compute magnetic declination for given location, elevation and time.
 * sign is such that true az = mag + declination.
 * if ok return true, else return false and, since out of date range is the only cause for failure,
 *    *mdp is set to the beginning year of valid 5 year period.
 */
bool magdecl (
    float l, float L,                   // geodesic lat, +N, long, +E, degrees
    float e,                            // elevation, m
    float y,                            // time, decimal year
    float *mdp                          // return magnetic declination, true degrees E of N 
)
{
        float alt = e/1000.;
        float dp, ti, gv;
        int maxdeg = 12;

        bool ok = E0000(&maxdeg,alt,l,L,y,mdp,&dp,&ti,&gv) == 0;

#ifdef _TEST_MAIN
        if (ok) {
            printf ("inclination %g\n", dp);
            printf ("total field %g nT\n", ti);
        }
#endif // _TEST_MAIN

        if (!ok)
            *mdp = ti;                  // return start of valid date range
        return (ok);
}

#ifdef _TEST_MAIN


/* stand-alone test program
 */

int main (int ac, char *av[])
{
        if (ac != 5) {
            char *slash = strrchr (av[0], '/');
            char *base = slash ? slash+1 : av[0];
            fprintf (stderr, "Purpose: test stand-alone magnetic declination model.\n");
            fprintf (stderr, "Usage: %s lat_degsN lng_degsE elevation_m decimal_year\n", base);
            exit(1);
        }

        float l = atof (av[1]);
        float L = atof (av[2]);
        float e = atof (av[3]);
        float y = atof (av[4]);
        float mdp;

        if (magdecl (l, L, e, y, &mdp))
            printf ("declination %g\n", mdp);
        else
            printf ("model only value from %g to %g\n", mdp, mdp+5);

        return (0);
}

#endif // _TEST_MAIN

