/* this is the html that runs in a browser showing a live hamclock connection.
 * the basic idea is to start with a complete image then continuously poll for incremental changes.
 * ESP is far too slow reading pixels to make this practical.
 */

#include "HamClock.h"

#if defined (_IS_UNIX)

char live_html[] =  R"_raw_html_(
<!DOCTYPE html>
<html>

<head>

    <title>
        HamClock Live!
    </title>
    
    <style>

        #hamclock-cvs {
            touch-action: pinch-zoom; /* allow both 1-finger moves and multi-touch p-z */
        }

    </style>

    <script>

        // config
        const UPDATE_MS = 200;          // update interval
        const MOUSE_HOLD_MS = 3000;     // mouse down duration to implement hold action
        const APP_W = 800;              // app coord system width
        const nonan_chars = ['Tab', 'Enter', 'Space', 'Escape', 'Backspace'];    // supported non-alnum chars

        // state
        var drawing_verbose = 0;        // > 0 for more info about drawing
        var web_verbose = 0;            // > 0 for more info about web commands
        var event_verbose = 0;          // > 0 for more info about keyboard or pointer activity
        var prev_regnhdr;               // for erasing if drawing_verbose > 1
        var app_scale = 0;              // size factor -- set for real when get first whole image
        var msec_corr;                  // msecs we should add to send to arrive at server at best time
        var pointerdown_ms = 0;         // Date.now when pointerdown event
        var pointermove_ms = 0;         // Date.now when pointermove event
        var cvs, ctx;                   // handy



        // called one time after page has loaded
        function onLoad() {

            console.log ("* onLoad");

            // handy access to canvas and drawing context
            cvs = document.getElementById('hamclock-cvs');
            ctx = cvs.getContext('2d', { alpha: false });   // faster w/o alpha

            // request one full screen image then schedule incremental update if ok else reload page.
            // also use the image info to set overall size and scaling.
            function getFullImage() {

                let update_sent = Date.now();
                sendGetRequest ('get_live.png', 'blob')
                .then (function (response) {
                    drawFullImage (response);
                    runSoon (getUpdate, 1);
                })
                .catch (function (err) {
                    console.log(err);
                    reloadThisPage();
                })
                .finally (function() {
                    if (web_verbose)
                        console.log ("  getFullImage took " + (Date.now() - update_sent) + " ms");
                });
            }


            // request one incremental update then schedule another if repeat else a full update
            function getUpdate (repeat) {

                let update_sent = Date.now();
                sendGetRequest ('get_live.bin', 'arraybuffer')
                .then (function (response) {
                    drawUpdate (response);
                    if (repeat)
                        runSoon (getUpdate, 1);
                })
                .catch (function (err) {
                    console.log(err);
                    runSoon (getFullImage, 0);
                })
                .finally (function() {
                    if (web_verbose)
                        console.log ("  getUpdate took " + (Date.now() - update_sent) + " ms");
                });
            }


            // given inherent hamclock build size, set canvas size and configure to stay centered
            function initCanvas (hc_w, hc_h) {
                if (drawing_verbose) {
                    console.log("document.documentElement.clientWidth = " + document.documentElement.clientWidth);
                    console.log("window.innerWidth = " + window.innerWidth);
                    console.log ("hamclock is " +  hc_w + " x " +  hc_h);
                }

                // pixels to draw on always match the real clock size
                cvs.width =  hc_w;
                cvs.height =  hc_h;

                // get window area dimensions
                let win_w = document.documentElement.clientWidth || window.innerWidth;
                let win_h = document.documentElement.clientHeight || window.innerHeight;

                // center if HC is smaller else shrink to fit
                if (hc_w < win_w && hc_h < win_h) {

                    // hc is smaller -- center in full screen
                    cvs.style.width = hc_w + "px";
                    cvs.style.height = hc_h + "px";

                } else {

                    // hc is larger -- shrink to fit preserving aspect
                    if (win_w*hc_h > win_h*hc_w) {
                        hc_w = hc_w*win_h/hc_h;
                        hc_h = win_h;
                    } else {
                        hc_h = hc_h*win_w/hc_w;
                        hc_w = win_w;
                    }
                    cvs.style.width = hc_w + "px";
                    cvs.style.height = hc_h + "px";
                }

                // center 
                cvs.style.position = 'absolute';
                cvs.style.top = "50%";
                cvs.style.left = "50%";
                cvs.style.margin = (-hc_h/2) + "px" + " 0 0 " + (-hc_w/2) + "px"; // trbl
                app_scale = hc_w/APP_W;

                if (drawing_verbose)
                    console.log ("canvas is " + hc_w + " x " + hc_h + " app_scale " + app_scale);
            }

            // display the given full png blob
            function drawFullImage (png_blob) {

                if (drawing_verbose)
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

                // extract 4-byte header preamble
                let aba = new Uint8Array(ab);
                const blok_w = aba[0];                          // block width, pixels
                const blok_h = aba[1];                          // block width, pixels
                msec_corr = 10*aba[2];                          // send time correction, ms
                const n_regn = (aba[3] << 8) | aba[4];          // n regns, MSB LSB

                if (drawing_verbose > 1) {
                    // erase, or at least unmark, previous marked regions
                    if (prev_regnhdr) {
                        ctx.strokeStyle = "black";
                        ctx.beginPath();
                        for (let i = 0; i < prev_regnhdr.length; i++) {
                            const cvs_x = prev_regnhdr[5+3*i] * blok_w;
                            const cvs_y = prev_regnhdr[6+3*i] * blok_h;
                            const cvs_w = prev_regnhdr[7+3*i] * blok_w;
                            ctx.rect (cvs_x, cvs_y, cvs_w, blok_h);
                        }
                        ctx.stroke();
                    }
                    // save for next time
                    prev_regnhdr = aba.slice(0,5+3*n_regn);
                }

                // walk down remainder of header and draw each region
                if (n_regn > 0) {
                    // remainder is one image blok_h hi of n_regns contiguous regions each variable width
                    let pngb = new Blob([aba.slice(5+3*n_regn)], {type:"image/png"});
                    createImageBitmap (pngb)
                    .then(function(ibm) {
                        // render each region.
                        let regn_x = 0;                         // walk region x along pngb
                        let n_draw = 0;                         // count n drawn regions just for stat
                        for (let i = 0; i < n_regn; i++) {
                            const cvs_x = aba[5+3*i] * blok_w;  // ul corner x in canvas pixels
                            const cvs_y = aba[6+3*i] * blok_h;  // ul corner y in canvas pixels
                            const n_long = aba[7+3*i];          // n regions long
                            const cvs_w = n_long * blok_w;      // total region width in canvas pixels
                            ctx.drawImage (ibm, regn_x, 0, cvs_w, blok_h, cvs_x, cvs_y, cvs_w, blok_h);

                            if (drawing_verbose > 1) {
                                // mark updated regions
                                if (drawing_verbose > 2)
                                    console.log (regn_x + " : " + cvs_x + "," + cvs_y + " "
                                                        + cvs_w + "x" + blok_h);
                                ctx.strokeStyle = "red";
                                ctx.beginPath();
                                ctx.rect (cvs_x, cvs_y, cvs_w, blok_h);
                                ctx.stroke();
                            }

                            regn_x += cvs_w;                    // next region
                            n_draw += n_long;
                        }
                        if (drawing_verbose)
                            console.log ("  drawUpdate " + aba.byteLength + "B " +
                                        n_regn + "/" + n_draw + " of " + blok_w + " x " + blok_h);

                    }).
                    catch(function(err) {
                        console.log("update promise err: ", err);
                        runSoon (getFullImage, 0);
                    });
                }

            }

            // schedule func(arg) soon
            var upd_tid = 0;                            // update pacing timer id
            function runSoon (func, arg) {

                // insure no nested requests
                if (upd_tid) {
                    if (web_verbose)
                        console.log ("cancel pending timer");
                    clearTimeout(upd_tid);
                }

                // register callback
                upd_tid = setTimeout (function(){upd_tid = 0; func(arg);}, UPDATE_MS);
                if (web_verbose)
                    console.log ("  set timer for " + func.name + " in " + UPDATE_MS + " ms");
            }

            // given any pointer event return coords with respect to canvas scaled to application.
            // returns undefined if scaling factor is not yet known.
            function getAppCoords (event) {

                if (app_scale) {
                    const rect = cvs.getBoundingClientRect();
                    const x = Math.round((event.clientX - rect.left)/app_scale);
                    const y = Math.round((event.clientY - rect.top)/app_scale);
                    return ({x, y});
                }
            }


            // send the given user event.
            function sendUserEvent (get) {

                if (event_verbose)
                    console.log ('sending ' + get);
                sendGetRequest (get, 'text')
                .then (function(response) {
                    // we used to do an immediate update for immediate feedback but with faster looping
                    // it just caused a large backlog
                    // getUpdate(0);
                })
                .catch (function (err) {
                    console.log(err);
                });
            }


            // record time of pointerdown
            cvs.addEventListener ('pointerdown', function(event) {
                pointerdown_ms = Date.now();
                event.preventDefault();
                if (event_verbose)
                    console.log ('pointer down');
            });

            // pointerup: send touch, hold depending on duration since pointerdown
            cvs.addEventListener ('pointerup', function(event) {
                event.preventDefault();

                // ignore if pointer moved
                if (!pointerdown_ms) {
                    if (event_verbose)
                        console.log ('cancel pointerup because pointer moved');
                    return;
                }

                // extract application coords
                const m = getAppCoords (event);

                // decide whether hold
                let pointer_dt = pointerdown_ms ? Date.now() - pointerdown_ms : 0;
                let hold = pointer_dt >= MOUSE_HOLD_MS;
                pointerdown_ms = 0;

                // compose and send
                let get = 'set_touch?x=' + m.x + '&y=' + m.y + (hold ? '&hold=1' : '&hold=0');
                sendUserEvent (get);
            });


            // connect pointermove to send set_mouse to hamclock
            cvs.addEventListener ('pointermove', function(event) {
                event.preventDefault();

                // not crazy fast
                let now = Date.now();
                let move_dt = now - pointermove_ms;
                if (move_dt < UPDATE_MS)
                    return;
                pointermove_ms = now;

                // extract application coords
                const m = getAppCoords (event);
                if (!m) {
                    console.log("pointermove: don't know app_scale yet");
                    return;
                }

                // cancel pointer down
                pointerdown_ms = 0;

                // compose and send
                let get = 'set_mouse?x=' + m.x + '&y=' + m.y;
                sendUserEvent (get);
            });


            // connect keydown to send character to hamclock, beware ctrl keys and browser interactions
            window.addEventListener('keydown', function(event) {

                // get char name
                let k = event.key;

                // a real space would create 'char= ' which doesn't parse so we invent Space name
                if (k === ' ')
                    k = 'Space';

                // ignore if modied
                if (event.metaKey || event.ctrlKey || event.altKey) {
                    if (event_verbose)
                        console.log('ignoring modified ' + k);
                    return;
                }

                // accept only certain non-alphanumeric keys
                if (k.length > 1 && !nonan_chars.find (e => { if (e == k) return true; })) {
                    if (event_verbose)
                        console.log('ignoring ' + k);
                    return;
                }

                // don't let browser see tab
                if (k === "Tab") {
                    if (event_verbose)
                        console.log ("stopping tab");
                    event.preventDefault();
                }

                // compose and send
                let get = 'set_char?char=' + k;
                sendUserEvent (get);
            });

            // respond to mobile device being rotated
            // window.addEventListener("orientationchange", function(event) {
            window.addEventListener("resize", function(event) {
                if (event_verbose)
                    console.log ("resize event");
                // get full image to establish new screen size
                runSoon (getFullImage, 0);
            });

            // reload this page as last resort, probably because server process restarted
            function reloadThisPage() {

                console.log ('* reloading');
                setTimeout (function() {
                    try {
                        window.location.reload(true);
                    } catch(err) {
                        console.log('* reload err: ' + err);
                    }
                }, 1000);
            }



            // return a Promise for the given url of the given type, passing response to resolve
            function sendGetRequest (url, type) {

                return new Promise(function (resolve, reject) {
                    if (web_verbose)
                        console.log ('sendGetRequest ' + url);
                    let xhr = new XMLHttpRequest();
                    xhr.open('GET', url);
                    xhr.responseType = type;
                    xhr.addEventListener ('load', function () {
                        if (xhr.status >= 200 && xhr.status < 300) {
                            if (drawing_verbose) {
                                if (type == 'text')
                                    console.log("  " + url + " reply: " + xhr.responseText);
                                else
                                    console.log("  " + url + " back ok");
                            }
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


            // all set. start things off with the full image, repeats from then on with updates forever.
            getFullImage();

        }

    </script>

</head>

<body onload='onLoad()' bgcolor='black' >

    <!-- page is a single canvas, size will be set based on hamclock build size -->
    <canvas id='hamclock-cvs'></canvas>

</body>
</html> 

)_raw_html_";

#endif
