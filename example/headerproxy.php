<?php
/**
 * Canonical example of a headerproxy you would proxy requests to
 *
 * Note that although not used in this example, the request the varnish sends
 * here will contain the following headers:
 *
 *    X-Forwarded-Method - The client's http request method (GET, POST, etc)
 *    X-Forwarded-Url - The client's http request url (ex. /about)
 */

$response = array(
    'vcl_recv' => array(),
    'vcl_deliver' => array(),
);

$response = setGeoLocation($response);
$response = setAbTest($response);

echo json_encode($response, JSON_PRETTY_PRINT);

/**
 * Simulates geographic location lookup.
 *
 * Algorithm is:
 *     10.x.x.x networks are "US"
 *     192.x.x.x networks are "UK"
 *
 * Set your IP with the http header X-Forwarded-For
 */
function setGeoLocation($response)
{
    if (isset($_COOKIE['geo'])) {
        # Client had geo cookie, saving an "expensive" geo lookup
        $geo = $_COOKIE['geo'];
    }
    else {
        # Lookup geo location based on IP
        $ip = (isset($_SERVER['HTTP_X_FORWARDED_FOR']) ? $_SERVER['HTTP_X_FORWARDED_FOR'] : '10.1.1.1');

        $ip = explode('.', $ip);

        switch ($ip[0]) {
            case "192":
                $geo = "UK";
                break;
            default:
                $geo = "US";
        }

        # Tell varnish to add the geo cookie to the final response. This allows
        # us to skip looking up the IP on future visits by this user.
        $response['vcl_deliver'][] = "Set-Cookie: geo=$geo";
    }

    # Tell varnish to add an X header to the client request. This will be passed
    # to the backend on cache misses, as well as used in the Vary header below.
    $response['vcl_recv'][] = "X-Geo: $geo";

    # Tell our backend to add a Vary header to the backend response. Notice
    # dont use a real Vary header since its the backend response that adds it.
    $response['vcl_recv'][] = "X-Vary: X-Geo";

    return $response;
}

/**
 * Simulates being in an AB test (testing different versions of html content)
 *
 * Normally AB test membership would be a random thing, but here we control
 * membership by adding the http header X-In-AB: 1 to the request. This will
 * trigger the alternate html content from the backend.
 */
function setAbTest($response)
{
    if (isset($_COOKIE['ab'])) {
        # Client had AB test cookie, so we keep test sticky
        $geo = $_COOKIE['geo'];
    }
    else {
        # Decide if we should be in test
        $ab = (isset($_SERVER['HTTP_X_IN_AB']) ? 1 : 0);

        # Tell varnish to add the AB test cookie to the final response. This
        # allows us to make the AB test decision sticky.
        $response['vcl_deliver'][] = "Set-Cookie: ab=$ab";
    }

    # Tell varnish to add an X header to the client request. This will be passed
    # to the backend on cache misses, as well as used in the Vary header below.
    $response['vcl_recv'][] = "X-Ab: $ab";

    # Tell our backend to add a Vary header to the backend response. Notice
    # dont use a real Vary header since its the backend response that adds it.
    $response['vcl_recv'][] = "Vary: X-Ab";

    return $response;
}
