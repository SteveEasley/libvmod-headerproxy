vcl 4.0;

import std;
import headerproxy;

backend default {
    .host = "127.0.0.1";
    .port = "8000";
}

sub vcl_recv {
    if (req.method != "GET" &&
      req.method != "HEAD" &&
      req.method != "PUT" &&
      req.method != "POST" &&
      req.method != "TRACE" &&
      req.method != "OPTIONS" &&
      req.method != "DELETE") {
        /* Non-RFC2616 or CONNECT which is weird. */
        return (pipe);
    }

    if (req.method != "GET" && req.method != "HEAD") {
        /* We only deal with GET and HEAD by default */
        return (pass);
    }

    # Call our headerproxy script. Notice we choose not to send requests for
    # asset files to the headerproxy.
    if (req.url !~ "\.(gif|jpg|png|css|js)(\?|$)") {
        # Make proxy call and add the desired request headers from your
        # vcl_recv json key.
        headerproxy.call(req.backend_hint, "/headerproxy.php");

        if (headerproxy.error()) {
            set req.http.X-HeaderProxy-Error = headerproxy.error();
        }
    }

    return (hash);
}

sub vcl_deliver {
    # This call adds the desired response headers from your vcl_deliver
    # json key.
    headerproxy.process();
}
