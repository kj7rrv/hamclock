/* this is the html that runs in a browser showing a live hamclock connection.
 * the basic idea is to start with a complete image then continuously poll for incremental changes.
 * ESP is far too slow reading pixels to make this practical.
 */

#include "HamClock.h"

#if defined (_IS_UNIX)

char live_html[] =  R"(
<!DOCTYPE html>
<html>

<head>

    <title>
        HamClock Live!
    </title>

    <script>

        const UPDATE_MS = 800;          // update interval a little faster than 1 Hz display, ms
        const verbose = 1;              // > 0  for more verbose; errors are always logged
        const regn_color = "red";       // region marker color if verbose > 1
        var prev_regnhdr;               // for erasing if verbose > 1
        var scale = 1;                  // size factor -- set for real when get first whole image
        var to_id;                      // timer id

        // called one time after page has loaded
        function onLoad() {

            // handy access to canvas and drawing context
            var cvs = document.getElementById('hamclock-cvs');
            var ctx = cvs.getContext('2d', { alpha: false });   // faster w/o alpha


            // request one full screen image then schedule incremental update if ok else another full.
            // also use the image info to set overall size and scaling.
            function getFullImage() {
                getRequest ('get_live.png', 'blob')
                .then (function (response) {
                    drawFullImage (response);
                    runSoon (getUpdate, 1);
                })
                .catch (function (err) {
                    console.log(err);
                    runSoon (getFullImage, 0);
                });
            }


            // request one incremental update then schedule another if repeat
            function getUpdate (repeat) {
                getRequest ('get_live.update', 'arraybuffer')
                .then (function (response) {
                    drawUpdate (response);
                    if (repeat)
                        runSoon (getUpdate, 1);
                })
                .catch (function (err) {
                    console.log(err);
                    runSoon (getFullImage, 0);
                });
            }


            // return a promise for the given url as the given type, passing response to resolve
            function getRequest (url, type) {
                return new Promise(function (resolve, reject) {
                    var xhr = new XMLHttpRequest();
                    xhr.open('GET', url);
                    xhr.responseType = type;
                    xhr.addEventListener ('load', function () {
                        if (xhr.status >= 200 && xhr.status < 300) {
                            resolve(xhr.response);
                        } else {
                            reject (url + ": " + xhr.status + " " + xhr.statusText);
                        }
                    });
                    xhr.addEventListener ('error', function () {
                        reject (url + ": " + xhr.status + " " + xhr.statusText);
                    });
                    xhr.send();
                });
            }

            // run func with arg after UPDATE_MS.
            // N.B. insure no more than one of these are queued
            function runSoon (func, arg) {
                if (verbose > 1)
                    console.log ("set timer for " + func.name);
                clearTimeout(to_id);
                to_id = setTimeout (func, UPDATE_MS, arg);
            }


            // init canvas size and configure to stay centered
            function initCanvas (w,h) {
                if (verbose)
                    console.log ("canvas " + w + " x " + h);
                scale = w/800;
                cvs.width = w;
                cvs.height = h;
                cvs.style.width = w + "px";
                cvs.style.height = h + "px";
                cvs.style.position = 'absolute';
                cvs.style.top = "50%";
                cvs.style.left = "50%";
                // top right bottom left
                cvs.style.margin = (-h/2) + "px" + " 0 0 " + (-w/2) + "px";
            }

            // display the given full png blob
            function drawFullImage (png_blob) {
                if (verbose)
                    console.log ("drawFullImage " + png_blob.size);
                createImageBitmap (png_blob)
                .then(function(ibm) {
                    initCanvas(ibm.width, ibm.height);
                    ctx.drawImage(ibm, 0, 0);
                })
                .catch(function(err){
                    console.log("full promise err: " + err);
                });
            }

            // display the given arraybuffer update
            function drawUpdate (ab) {
                // extract header preamble
                var aba = new Uint8Array(ab);
                const regn_w = aba[0];
                const regn_h = aba[1];
                const n_regn = (aba[2] << 8) | aba[3];

                if (verbose > 1) {
                    // erase, or at least unmark, previous marked regions
                    if (prev_regnhdr) {
                        ctx.strokeStyle = "black";
                        ctx.beginPath();
                        for (var i = 0; i < n_regn; i++) {
                            const cvs_x = prev_regnhdr[4+2*i] * regn_w;
                            const cvs_y = prev_regnhdr[5+2*i] * regn_h;
                            ctx.rect (cvs_x, cvs_y, regn_w, regn_h);
                        }
                        ctx.stroke();
                    }
                    // copy for next time
                    prev_regnhdr = aba.slice(0,4+2*n_regn);
                }

                // walk down remainder of header to get locations of region images
                if (n_regn > 0) {
                    // extract remainder as one image containing one column of n_regn little images
                    var pngb = new Blob([aba.slice(4+2*n_regn)], {type:"image/png"});
                    createImageBitmap (pngb)
                    .then(function(ibm) {
                        // render each region. image is in column stripes so coalesce rows to reduce draws
                        const regn_x = 0;
                        var regn_y = 0;
                        var cvs_co_x = aba[4] * regn_w;
                        var cvs_co_y = aba[5] * regn_h;
                        var co_h = regn_h;
                        var n_draw = 0;
                        for (var i = 1; i < n_regn; i++) {
                            const cvs_x = aba[4+2*i] * regn_w;
                            const cvs_y = aba[5+2*i] * regn_h;
                            if (cvs_x == cvs_co_x && cvs_y == cvs_co_y + co_h) {
                                // extend this column
                                co_h += regn_h;
                            } else {
                                // draw region
                                ctx.drawImage (ibm, regn_x, regn_y, regn_w, co_h,
                                                   cvs_co_x, cvs_co_y, regn_w, co_h);
                                n_draw++;
                                // start next column
                                cvs_co_x = cvs_x;
                                cvs_co_y = cvs_y;
                                regn_y += co_h;
                                co_h = regn_h;
                            }

                            if (verbose > 1) {
                                // mark updated regions
                                ctx.strokeStyle = regn_color;
                                ctx.beginPath();
                                ctx.rect (cvs_x, cvs_y, regn_w, regn_h);
                                ctx.stroke();
                            }
                        }

                        // always leaves one more undrawn column
                        ctx.drawImage (ibm, regn_x, regn_y, regn_w, co_h,
                                               cvs_co_x, cvs_co_y, regn_w, co_h);
                        n_draw++;

                        if (verbose)
                            console.log ("getUpdate " + aba.byteLength + ": draw " + n_draw + "/" + n_regn
                                                + " " + regn_w + "x" + regn_h);

                    }).
                    catch(function(err) {
                        console.log("update promise err: ", err);
                        setTimeout(getFullImage, UPDATE_MS, 1);
                    });
                }

            }

            // connect onClick to send click to hamclock
            cvs.addEventListener('click', function(event) {
                // extract canvas coords and whether metakey was pressed
                var rect = cvs.getBoundingClientRect();
                var x = Math.round((event.clientX - rect.left)/scale);
                var y = Math.round((event.clientY - rect.top)/scale);
                var hold = event.metaKey;
                var get = 'set_touch?x=' + x + '&y=' + y + (hold ? '&hold=1' : '&hold=0');
                if (verbose)
                    console.log (get);

                // send set_touch event
                var xhr = new XMLHttpRequest();
                xhr.responseType = 'text';
                xhr.addEventListener ('load', function() {
                    if (verbose)
                        console.log(get + " back: " + xhr.responseText);
                    // one-time update for immediate visual feedback
                    getUpdate(0);
                });
                xhr.open('GET', get);
                xhr.send(null);
            });

            // connect keydown to send character to hamclock
            window.addEventListener('keydown', function(event) {

                // get char; space causes 'char= ' which doesn't parse so we invent Space name
                var k = event.key;
                if (k === ' ')
                    k = 'Space';
                var get = 'set_char?char=' + k; // can also be things like Tab, Enter, Backspace
                if (verbose)
                    console.log (get);

                // send set_char event
                var xhr = new XMLHttpRequest();
                xhr.responseType = 'text';
                xhr.addEventListener ('load', function() {
                    if (verbose)
                        console.log(get + " back: " + xhr.responseText);
                    // one-time update for immediate visual feedback
                    getUpdate(0);
                });
                xhr.open('GET', get);
                xhr.send(null);

                // don't let browser see tab
                if (k === "Tab") {
                    if (verbose)
                        console.log ("stop tab");
                    event.preventDefault();
                }

            });

            // get first image, repeats from then on
            getFullImage();

        }

    </script>

</head>

<body onload='onLoad()' bgcolor='black' >

    <!-- page is a single centered canvas, size will be set to match hamclock build size -->
    <canvas id='hamclock-cvs'></canvas>

</body>
</html> 
)";

#endif
