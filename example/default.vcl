vcl 4.0;

import std;
import headerproxy;

backend default {
    .host = "localhost";
    .port = "8000";
}

sub vcl_recv {
    headerproxy.path("/headerproxy.php");

    # We choose not to send ajax requests, or requests for assets to the header proxy
    if (false == (
            req.http.X-Requested-With == "XMLHttpRequest" ||
            req.url ~ "\.(gif|jpg|jpeg|png|swf|css|js|flv|mp3|mp4|pdf|ico|json|woff|ttf|eot|svg)(\?|$)")) {
        headerproxy.call();
    }

    if (headerproxy.error()) {
        set req.http.X-Vmod-HeaderProxy-Error = headerproxy.error();
    }
}

sub vcl_backend_response {
    headerproxy.process();

    # Combine multiple Vary headers coming from both vcl_recv and backend response
    std.collect(beresp.http.Vary);
}

sub vcl_backend_error {
    # This call is absolutely required since its a potential exit point for requests
    headerproxy.process();
}

sub vcl_deliver {
    # This call is absolutely required since its a potential exit point for requests
    headerproxy.process();
}

sub vcl_pipe {
    # This call is absolutely required since its a potential exit point for requests
    headerproxy.process();
}

sub vcl_synth {
    # This call is absolutely required since its a potential exit point for requests
    headerproxy.process();
}
