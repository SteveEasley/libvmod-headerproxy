vcl 4.0;

import std;
import headerproxy;

backend default {
    .host = "localhost";
    .port = "8000";
}

sub vcl_recv {
    # We choose not to send ajax requests, or requests for assets to the header proxy
    if (false == (
            req.http.X-Requested-With == "XMLHttpRequest" ||
            req.url ~ "\.(gif|jpg|png|css|js)(\?|$)")) {

        # This call adds client request headers
        headerproxy.call(req.backend_hint, "/headerproxy.php");

        if (headerproxy.error()) {
            set req.http.X-Vmod-HeaderProxy-Error = headerproxy.error();
        }
    }
}

sub vcl_deliver {
    # This call adds client response headers.
    headerproxy.process();
}
