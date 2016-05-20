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
 * An example of a backend web service (mini framework FTW)
 */

// $request will be an array of url components
$request = onRequest($_SERVER['REQUEST_URI']);

// Get response object from our controller action
$response = indexAction($request);

// Send reponse
echo onResponse($response);

/**
 * Return parsed $url array
 */
function onRequest($url)
{
    $request = parse_url($url);

    $request['params'] = array();

    if (isset($request['query'])) {
        parse_str($request['query'], $request['params']);
    }

    return $request;
}

/**
 * Set headers and return html content from $response
 */
function onResponse($response)
{
    header('HTTP/1.1 200 OK');
    header('Content-Length: ' . strlen($response['content']));
    header('Cache-Control: ' . ($response['context']['cache'] ? 'public, max-age=120' : 'no-cache'));
    if ($response['context']['vary'])
        header('Vary: ' . $response['context']['vary']);
    return $response['content'];
}

/**
 * Simulated MVC controller actions
 */
function indexAction($request)
{
    # User's geographic location. This comes from the headerproxy.php script.
    # Example: X-Geo: us
    $geoLoc = isset($_SERVER['HTTP_X_GEO']) ? $_SERVER['HTTP_X_GEO'] : null;

    # User's AB test they are in, if any. This comes from the headerproxy.php script.
    # Example: X-AB: milk
    $inAbTest = isset($_SERVER['HTTP_X_AB']) && $_SERVER['HTTP_X_AB'];

    # What header names we should vary on.
    # Example: X-Vary: X-Geo, X-AB
    $vary = isset($_SERVER['HTTP_X_VARY']) ? $_SERVER['HTTP_X_VARY'] : null;

    # A session cookie simulates a logged in user. We dont cache logged in users.
    $session = (isset($_COOKIE['session']) ? $_COOKIE['session'] : false);

    # Simple PHP template and context
    $response = array(
        'content' => "
            <html>
                <head>
                    <title>Home | %GEOLOC%</title>
                </head>
                <body>
                    <h1>Home Page</h1>
                    <p>Welcome %WHO%!</p>
                    <p>%CONTENT%</p>
                </body>
            </html>",
        'context' => array(
            'cache' => ($session ? false : true),
            'vary' => $vary
        )
    );

    # Different geo locations should have different cached responses
    $response['content'] = str_replace('%GEOLOC%', $geoLoc, $response['content']);

    # Customize getting. Guests responses will be cached. Logged in users wont.
    $response['content'] = str_replace('%WHO%', ($session ? $session : 'guest'), $response['content']);

    # Simulate being in an AB test group, or not
    $response['content'] = str_replace('%CONTENT%', ($inAbTest ? "Drink more milk" : "Drink milk"), $response['content']);

    # Pretty up content for viewing source
    $response['content'] = ltrim($response['content'], "\r\n") . "\n";

    return $response;
}
