<?php
/**
  * A canonical example of a backend web service
  */

$request = onRequest(parse_url($_SERVER['REQUEST_URI']));

$response = indexAction($request);

echo onResponse($response);

function onRequest($request)
{
    $request['params'] = array();

    if (isset($request['query'])) {
        parse_str($request['query'], $request['params']);
    }

    return $request;
}

function onResponse($response)
{
    header('HTTP/1.1 200 OK');
    header('Cache-Control: ' . ($response['cache'] ? 'public, max-age=120' : 'no-cache'));

    return $response['content'];
}

function indexAction($request)
{
    # Geographic location. Set in Varnish via vmod_headerproxy
    $geoLoc = $_SERVER['HTTP_X_GEO'];

    # Which test user is in, if any. Set in Varnish via vmod_headerproxy
    $inAbTest = isset($_SERVER['HTTP_X_AB']);

    # A session cookie simulates a logged in user, something we don't cache
    $session = (isset($_COOKIE['session']) ? $_COOKIE['session'] : false);

    $response = array(
        'content' => "
            <html>
                <head>
                    <title>Home | %GEOLOC%</title>
                </head>
                <body>
                    <h1>Home Page</h1>
                    <p>Welcome %WHO%!</p>
                    <p>%ABTEST%</p>
                </body>
            </html>",
        'cache' => ($session ? false : true)
    );

    # Different geo locations should have different cached responses
    $response['content'] = str_replace('%GEOLOC%', $geoLoc, $response['content']);

    # Customize getting. Guests responses will be cached. Logged in users wont.
    $response['content'] = str_replace('%WHO%', ($session ? $session : 'guest'), $response['content']);

    # Simulate being in an AB test group, or not
    $response['content'] = str_replace('%ABTEST%', ($inAbTest ? "Drink more milk" : "Drink milk"), $response['content']);

    # Pretty up content for viewing
    $response['content'] = ltrim($response['content'], "\r\n") . "\n";

    return $response;
}
