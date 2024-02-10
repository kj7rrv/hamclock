/* this is the html that runs in a browser showing a live hamclock connection.
 * the basic idea is to start with a complete image then continuously poll for incremental changes.
 * this page is loaded first with all subsequent communication via a websocket.
 * ESP is far too slow reading pixels to make this practical.
 */

#include "HamClock.h"

#if defined (_IS_UNIX)

char live_html[] =  R"_raw_html_(
<!DOCTYPE html>
<html>

<head>

    <!-- this might help iOS safari run full screen -->
    <meta name="apple-mobile-web-app-capable" content="yes" />
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent" />

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
        const UPDATE_MS = 100;          // update interval
        const MOUSE_JITTER = 5;         // allow this much mouse motion for a touch
        const MOUSE_HOLD_MS = 3000;     // mouse down duration to implement hold action
        const APP_W = 800;              // app coord system width
        const nonan_chars = ['Tab', 'Enter', 'Space', 'Escape', 'Backspace'];    // supported non-alnum chars

        // state
        var ws;                         // Websocket
        var ws_abdata = 0;              // ws onmessage header capture
        var drawing_verbose = 0;        // > 0 for more info about drawing
        var ws_verbose = 0;             // > 0 for more info about websocket commands
        var event_verbose = 0;          // > 0 for more info about keyboard or pointer activity
        var prev_regnhdr;               // for erasing if drawing_verbose > 1
        var app_scale = 0;              // size factor -- set for real when get first whole image
        var pointerdown_ms = 0;         // Date.now when pointerdown event
        var pointerdown_x = 0;          // location of pointerdown event
        var pointerdown_y = 0;          // location of pointerdown event
        var pointermove_ms = 0;         // Date.now when pointermove event
        var fs_success = 0;             // whether setting full screen has ever succeeded
        var cvs, ctx;                   // handy

        // define functions, onLoad follows near the bottom

        // request a new full image
        function getFullImage() {
            sendWSMsg ("get_live.png?");
        }

        // request an image update
        function getUpdate() {
            sendWSMsg ("get_live.bin?");
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

        // display the given full png uint8
        function drawFullImage (png8) {

            var pngbl = new Blob ([png8], {type:"image/png"});
            createImageBitmap (pngbl)
            .then(function(ibm) {
                if (drawing_verbose)
                    console.log ("drawFullImage size " + ibm.width + " x " + ibm.height);
                initCanvas(ibm.width, ibm.height);
                ctx.drawImage(ibm, 0, 0);
            })
            .catch(function(err){
                console.log("full image promise err: " + err);
            });
        }

        // update given header and png sprites image -- see liveweb.cpp::updateExistingClient()
        function drawUpdate (hdr8, png8) {

            // extract 4-byte header preamble
            const blok_w = hdr8[0];                         // block width, pixels
            const blok_h = hdr8[1];                         // block width, pixels
            const n_regn = (hdr8[2] << 8) | hdr8[3];        // total n blocks wide, MSB LSB

            if (drawing_verbose > 1) {
                // erase, or at least unmark, previous marked regions
                if (prev_regnhdr) {
                    ctx.strokeStyle = "black";
                    ctx.beginPath();
                    for (let i = 0; i < prev_regnhdr.length; i++) {
                        const cvs_x = prev_regnhdr[4+3*i] * blok_w;
                        const cvs_y = prev_regnhdr[5+3*i] * blok_h;
                        const cvs_w = prev_regnhdr[6+3*i] * blok_w;
                        ctx.rect (cvs_x, cvs_y, cvs_w, blok_h);
                    }
                    ctx.stroke();
                }
                // save for next time
                prev_regnhdr = hdr8.slice(0,4+3*n_regn);
            }

            // walk down remainder of header and draw each region
            if (n_regn > 0) {
                // png8 is one image blok_h hi of n_regns contiguous regions each variable width
                let pngbl = new Blob ([png8], {type:"image/png"});
                createImageBitmap (pngbl)
                .then(function(ibm) {
                    // render each region.
                    let regn_x = 0;                         // walk region x along img
                    let n_draw = 0;                         // count n drawn regions just for stat
                    for (let i = 0; i < n_regn; i++) {
                        const cvs_x = hdr8[4+3*i] * blok_w; // ul corner x in canvas pixels
                        const cvs_y = hdr8[5+3*i] * blok_h; // ul corner y in canvas pixels
                        const n_long = hdr8[6+3*i];         // n regions long
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
                        console.log ("  drawUpdate " + hdr8.byteLength + "B " +
                                    n_regn + "/" + n_draw + " of " + blok_w + " x " + blok_h);

                })
                .catch(function(err) {
                    console.log("update promise err: ", err);
                    runSoon (getFullImage);
                });
            }

        }

        // schedule func() soon
        var upd_tid = 0;                            // update pacing timer id
        function runSoon (func) {

            // insure no nested requests
            if (upd_tid) {
                if (ws_verbose)
                    console.log ("cancel pending timer");
                clearTimeout(upd_tid);
            }

            // register callback
            upd_tid = setTimeout (function(){upd_tid = 0; func();}, UPDATE_MS);
            if (ws_verbose)
                console.log ("setting timer for " + func.name + " in " + UPDATE_MS + " ms");
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

        // send the given key to the hamclock
        function sendKey (k) {
            if (event_verbose)
                console.log('sending ' + k);
            sendWSMsg ('set_char?char=' + k);
        }


        // connect keydown to send character to hamclock, beware ctrl keys and browser interactions
        window.addEventListener('keydown', function(event) {

            // get char name or map arrow keys to vi
            var key;
            switch (event.key) {
            case 'ArrowLeft':  key = 'h'; break;
            case 'ArrowDown':  key = 'j'; break;
            case 'ArrowUp':    key = 'k'; break;
            case 'ArrowRight': key = 'l'; break;
            default:           key = event.key;
            }

            // a real space would send 'char= ' which doesn't parse so we invent Space name
            if (key === ' ')
                key = 'Space';

            // ignore if modified
            if (event.metaKey || event.ctrlKey || event.altKey) {
                if (event_verbose)
                    console.log('ignoring modifier ' + key);
                return;
            }

            // accept only certain non-alphanumeric keys
            if (key.length > 1 && !nonan_chars.find (e => { if (e == key) return true; })) {
                if (event_verbose)
                    console.log('ignoring ' + key);
                return;
            }

            // don't let browser see tab
            if (key === "Tab") {
                if (event_verbose)
                    console.log ("stopping tab");
                event.preventDefault();
            }

            sendKey (key);
        });

        // respond to mobile device being rotated. resize seems to work better than orientationchange
        // window.addEventListener("orientationchange", function(event) {
        window.addEventListener("resize", function(event) {
            if (event_verbose)
                console.log ("resize event");
            // get full image to establish new screen size
            runSoon (getFullImage);
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
            }, 2000);
        }



        // send the given message over the websocket
        function sendWSMsg (msg) {
            if (ws.readyState != 1) {
                setTimeout (sendWSMsg, 500, msg);
            } else {
                if (ws_verbose)
                    console.log ('sendWSMsg ' + msg);
                ws.send (msg);
            }
        }

        // called one time after page has loaded
        function onLoad() {

            // create websocket back to same location replacing last component of our path with "live-ws"
            let ws_proto = (location.protocol === "https:") ? "wss://" : "ws://";       // tnx WK2X
            let ws_host = location.host + location.pathname.replace(/\/[^\/]*$/, "/live-ws");
            ws = new WebSocket ( ws_proto + ws_host);
            ws.binaryType = "arraybuffer";
            ws.onopen = function () {
                if (ws_verbose)
                    console.log('WS connection established.');
            };
            ws.onclose = function () {
                if (ws_verbose)
                    console.log('WS connection closed.');
                reloadThisPage();
            };
            ws.onerror = function (e) {
                console.log('WS connection failed: ', e);
                reloadThisPage();
            };


            // respond to hamclock messages
            ws.onmessage = function (e) {
                if (ws_verbose)
                    console.log ('ws onmessage length ' + e.data.byteLength);
                if (e.data instanceof ArrayBuffer) {
                    var data8 = new Uint8Array (e.data);
                    if (data8[0] == 137 && data8[1] == 80 && data8[2] == 78 && data8[3] == 71) {
                        // this is a PNG image -- show whole if alone else assume its part of an update
                        if (ws_abdata) {
                            drawUpdate (new Uint8Array(ws_abdata), data8);
                            ws_abdata = 0;
                        } else {
                            drawFullImage (data8);
                        }
                        // ask for updates regardless
                        runSoon (getUpdate);
                    } else if (data8[0] == 99 && data8[1] == 91) {      // see liveweb.cpp
                        // this is a message whether to be in full screen mode.
                        // only succeed once in case user wants to cancel
                        if (data8[2]) {
                            if (!fs_success) {
                                if (document.fullscreenElement)
                                    fs_success = 1;
                                else
                                    document.documentElement.requestFullscreen();
                            }
                        }
                    } else {
                        // this is an update patch collection
                        ws_abdata = e.data;
                    }
                }
            }
            
            // handy access to canvas and drawing context
            cvs = document.getElementById('hamclock-cvs');
            ctx = cvs.getContext('2d', { alpha: false });       // faster w/o alpha
            ctx.translate(0.5, 0.5);                            // a tiny bit less blurry?

            // pointerdown: record time and position
            cvs.addEventListener ('pointerdown', function(event) {
                // all ours
                event.preventDefault();

                const m = getAppCoords (event);
                if (!m) {
                    console.log("pointerdown: don't know app_scale yet");
                    return;
                }

                pointerdown_ms = Date.now();
                pointerdown_x = m.x;
                pointerdown_y = m.y;
                if (event_verbose)
                    console.log ('pointer down');
            });

            // pointerleave: send illegal mouse location 
            cvs.addEventListener ('pointerleave', function(event) {
                // all ours
                event.preventDefault();

                // send location well outside app
                sendWSMsg ('set_mouse?x=-1&y=-1');

            });

            // pointerup: send touch, hold depending on duration since pointerdown
            cvs.addEventListener ('pointerup', function(event) {
                // all ours
                event.preventDefault();

                // extract application coords
                const m = getAppCoords (event);
                if (!m) {
                    console.log("pointerup: don't know app_scale yet");
                    return;
                }

                // ignore if pointer moved
                if (Math.abs(m.x-pointerdown_x) > MOUSE_JITTER || Math.abs(m.y-pointerdown_y) > MOUSE_JITTER){
                    if (event_verbose)
                        console.log ('cancel pointerup because pointer moved');
                    return;
                }

                // decide whether hold
                let pointer_dt = pointerdown_ms ? Date.now() - pointerdown_ms : 0;
                let hold = pointer_dt >= MOUSE_HOLD_MS;
                pointerdown_ms = 0;

                // compose and send
                let msg = 'set_touch?x=' + m.x + '&y=' + m.y + (hold ? '&hold=1' : '&hold=0');
                sendWSMsg (msg);
            });


            // pointermove: send set_mouse
            cvs.addEventListener ('pointermove', function(event) {
                // all ours
                event.preventDefault();

                // not crazy fast
                let now = Date.now();
                if (pointermove_ms + UPDATE_MS > now)
                    return;
                pointermove_ms = now;

                // extract application coords
                const m = getAppCoords (event);
                if (!m) {
                    console.log("pointermove: don't know app_scale yet");
                    return;
                }

                // compose and send
                let msg = 'set_mouse?x=' + m.x + '&y=' + m.y;
                sendWSMsg (msg);
            });

            document.addEventListener('paste', e=>{
                let str = e.clipboardData.getData('text/plain');
                for (let i = 0; i < str.length; i++) {
                    sendKey(str[i]);
                }
            });


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
