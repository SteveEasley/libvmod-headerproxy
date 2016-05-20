<?php
/**
 * Copyright (c) 2014 Steve Easley <tardis74@yahoo.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * An example of a headerproxy you would have Varnish libvmod_headerproxy send
 * requests to.
 */

# Not used in this example, but demonstrates what headers are available
$ip = isset($_SERVER['HTTP_X_FORWARDED_FOR']) ? $_SERVER['HTTP_X_FORWARDED_FOR'] : null;
$url = isset($_SERVER['HTTP_X_FORWARDED_URL']) ? $_SERVER['HTTP_X_FORWARDED_URL'] : null;
$method = isset($_SERVER['HTTP_X_FORWARDED_METHOD']) ? $_SERVER['HTTP_X_FORWARDED_METHOD'] : null;

# A handy array to hold all our headers. Note this is a different format
# than libvmod_headerproxy expects, but will be converted in getResponse()
# below.
$context = array(
    'headers' => array(),   # Headers to add to client request
    'vary' => array(),      # Header names to Vary response on
    'cookies' => array(),   # Cookies to add to client response
);

setGeoLocation($context);
setAbTest($context);

header('HTTP/1.1 200 OK');
header('Content-Type: application/json');
echo getResponse($context);

/**
 * Simulates geographic location lookup.
 *
 * Fake lookup algorithm is:
 *     10.x.x.x networks are "US"
 *     192.x.x.x networks are "UK"
 *
 * Set your IP with the http header X-Forwarded-For like:
 *   curl -H "X-Forwarded-For: 10.1.1.1" "http://localhost/"
 */
function setGeoLocation(&$context)
{
    if (isset($_COOKIE['geo'])) {
        # Client had geo cookie from a previous request, so we get to skip a
        # potentially expensive geo lookup below by using their value.
        $geo = $_COOKIE['geo'];
    }
    else {
        # Lookup geo location based on IP
        $ip = (isset($_SERVER['HTTP_X_FORWARDED_FOR'])
            ? $_SERVER['HTTP_X_FORWARDED_FOR'] : '10.1.1.1');

        $ip = explode('.', $ip);

        switch ($ip[0]) {
            case '192':
                $geo = 'UK';
                break;
            default:
                $geo = 'US';
        }

        # Tell varnish to add the geo cookie to the final response. This allows
        # us to skip looking up the IP on future visits by this user.
        $context['cookies']['geo'] = $geo;
    }

    # Tell Varnish to add an X-Geo header to the client request. Varnish will
    # pass this to the backend on cache misses. It's name is referenced by the
    # Vary header below.
    $context['headers']['X-Geo'] = $geo;

    # Tell Varnish to add an X-Vary header to the client request. On cache
    # misses the request will be passed by Varnish to the backend where
    # index.php will convert this to the official response Vary header, which
    # will be used to create a separate cache object based on the geo location
    # they are in.
    $context['vary'][] = 'X-Geo';
}

/**
 * Simulates being in an AB test (testing different versions of html content)
 *
 * Normally AB test membership would be a random thing. For demonstration
 * purposes you can force yourself into the test by adding the request header
 * Put-In-AB-Test. Example:
 *   curl -H "Put-In-AB-Test: 1" "http://localhost/"
 */
function setAbTest(&$context)
{
    if (isset($_COOKIE['ab'])) {
        # Client already in test
        $ab = $_COOKIE['ab'];
    }
    else {
        # Decide if we should be in test
        $ab = (isset($_SERVER['HTTP_PUT_IN_AB_TEST']) && $_SERVER['HTTP_PUT_IN_AB_TEST']) ? 1 : 0;

        # Tell varnish to add the AB test cookie to the client response. This
        # allows us to make the AB test decision sticky.
        $context['cookies']['ab'] = $ab;
    }

    # Tell Varnish to add an X-Geo header to the client request. Varnish will
    # pass this to the backend on cache misses. It's name is referenced by the
    # Vary header below.
    $context['headers']['X-AB'] = $ab;

    # Tell Varnish to add an X-Vary header to the client request. On cache
    # misses the request will be passed by Varnish to the backend where
    # index.php will convert this to the official response Vary header, which
    # will be used to create a separate cache object based on the AB test
    # they are in.
    $context['vary'][] = 'X-AB';
}

/**
 * Convert $context to json encoded string for libvmod_headerproxy
 */
function getResponse($context)
{
    $response = [
        'vcl_recv' => array(),
        'vcl_deliver' => array(),
    ];

    # Add headers
    if (count($context['headers'])) {
        foreach ($context['headers'] as $name => $value) {
            $response['vcl_recv'][] = sprintf('%s: %s', $name, $value);
        }
    }

    # Add X-Vary header so index.php can convert it to the official Vary header
    # on backend response
    if (count($context['vary'])) {
        $response['vcl_recv'][] = sprintf('X-Vary: %s', implode(', ', $context['vary']));
    }

    # Add cookie headers
    if (count($context['cookies'])) {
        foreach ($context['cookies'] as $name => $value) {
            $response['vcl_deliver'][] = sprintf('Set-Cookie: %s=%s', $name, $value);
        }
    }

    return json_encode($response);
}
