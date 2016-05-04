================
vmod_headerproxy
================

-------------------------
Varnish Header Proxy VMOD
-------------------------

:Author: Steve Easley
:Date: 2015-07-22
:Version: 0.1.0
:Manual section: 3

This vmod requires Varnish 4.1 or higher.

SYNOPSIS
========
::

    import headerproxy;

    sub vcl_recv {
        headerproxy.call(``BACKEND``, ``PATH``);
    }

    sub vcl_deliver {
        headerproxy.process();
    }

DESCRIPTION
===========

Varnish vmod that proxies every client request coming into Varnish to a url
running the scripting language of your choice (PHP, Node, Python, etc), and
subsequently adds custom headers to the client request and response. Your web
script controls the headers that are added. This super cool ability enables you
to:

* Do additional processing on every client request inside your web script, even
  cached requests! Examples include logging, data warehousing, etc.
* Move complex business logic for controlling caching decisions out of Varnish
  VCL and into your web script. Examples include varying responses on geo
  location, AB testing, etc.

You might be asking yourself, why would I add latency to my super fast cached
responses by adding an additional HTTP call? That's a great question. The
answer is yes, it does add latency, but the benefit is allowing you to cache
requests where you might not have been able them at all before. It's up to you
to decide if its worth in.

Note that the web script you proxy requests to does not actually modify the
client request or response directly. Instead your script outputs a json
formatted list of headers it wants the vmod to add.

Check out the canonical `example <example/>`_.

USAGE
=====

Usage involves:

1. Writing and hosting the web script you want to proxy requests to. See
   `WEB SCRIPT`_ for details.
2. Add the appropriate vmod functions to your vcl. See `VCL`_ for details.

WEB SCRIPT
==========

You must host a web script that the vmod can proxy requests to. This script is
something you write. It will be called by the vmod and must return a response
containing any headers it wants added, if any.

HTTP request from vmod
----------------------

The vmod will call your web script with an HTTP GET request containing a copy
of the same headers the client sent. The exception to this is the HTTP
``method`` and ``url`` headers. For those you will receive these additional
headers:

X-Forwarded-Method
    Contains HTTP method requested by the client (``GET``, ``POST``, ``PUT``,
    etc).

X-Forwarded-Url
    Contains the ``url`` requested by the client.

X-Forwarded-For
    Contains client IP.

HTTP response back to vmod
--------------------------
Your web script must return a ``Content-Type: application/json`` response whose
body is a json object with the headers it wants the vmod to set in ``vcl_recv``
and ``vcl_deliver``. Here is an example response you might return::

    {
        'vcl_recv': {
            'X-Geo: us',
            'X-Device: tablet'
        },
        'vcl_deliver': {
            'Set-Cookie: geo=us',
            'Set-Cookie: device=tablet',
        }
    }

In this example you are requesting that inside ``vcl_recv`` the vmod add two
headers to the client ``request``, and inside vcl_deliver add two cookies to
the client ``response``.

The vmod only accepts 200 HTTP response codes. If you return anything else the
response will be ignored.

Script example
--------------
::

    <?php
    if (false == isset($_COOKIE['geo'])) {
        $geo = geo_lookup($_SERVER['HTTP_X_FORWARDED_FOR']);
    }

    header('Content-type: application/json');

    echo json_encode(array(
        'vcl_recv' => array(
            "X-Geo: $geo",
        ),
        'vcl_deliver' => array(
            "Set-Cookie: geo=$geo"
        )
    ));

This example shows how you can prepare a request to vary a response on geo
location. First we do the geo lookup. Next, via the JSON, we request an
``X-Geo`` header be added to the client request. Finally we request a
``Set-Cookie`` header be added to the response going to the client. Setting a
cookie allows us to bypass the potentially expensive geo lookup at the top of
the script.

On the Varnish side, here is what a request that goes to the backend might look
like after the ``headerproxy.call()`` call in ``vcl_recv``. The X-Geo header
was automatically inserted by the vmod.::

    GET /index.html HTTP/1.1
    Host: www.example.com
    X-Geo: us

And here is a possible response from the backend. Note that your backend app
(not the vmod web script) needs to add the Vary header. Its up to you how to
implement this logic.::

    HTTP/1.1 200 OK
    Content-type: text/html
    Vary: X-Geo

And here is the response to the client after the ``headerproxy.process()`` call
in ``vcl_deliver``. The Set-Cookie header was automatically inserted by the
vmod.::

    HTTP/1.1 200 OK
    Content-type: text/html
    Set-Cookie: geo=us

VCL
===

To call your web script you first add a ``headerproxy.call()`` call into
``vcl_recv`` (see `call`_). This method takes two parameters.

The first parameter to ``headerproxy.call()`` is a Varnish backend (or
director). The vmod will use this backend to determine the hostname of the
server hosting your web script. It can be dedicated backed/director just for
your web script, or you can use the same backend used by your application
backends (by passing ``req.backend_hint`` as a param).

The second parameter to ``headerproxy.call()`` is a string containing the path
to your script. For example "/webscript".

Calling ``headerproxy.call()`` does the following:

* Using curl the vmod sends the client request to the url of your web
  script. Your script will get an identical copy of all client request
  headers (see `Request from vmod`_).
* Your web script will return a list of headers that the vmod will add
  to the request (see `Response back to vmod`_).
* The vmod will insert the headers specified in a ``vcl_recv`` json key
  into the client ``request``. TIP: Headers you add here can be
  referenced by a ``Vary`` response header, which is where the real
  power comes in.

Finally you add a ``headerproxy.process()`` in ``vcl_deliver`` (see
`process`_).  The vmod will insert the headers requested in a ``vcl_deliver``
json key into the client ``response``. TIP: Headers set here wont be cached.
Its the ideal place to insert ``Set-Cookie`` headers.


FUNCTIONS
=========

call
----

Prototype
    ::

        headerproxy.call(BACKEND backend, STRING path)

Context
    vcl_recv

Returns
	VOID

Description
	Tells the vmod to proxy the client request to your web script then inserts
	the	requested ``request`` headers from your json response. Based on your vcl
	logic you can opt to not proxy the request by simply not calling
	``headerproxy.call``.

Example
    ::

        # Proxy requests to the same backend servers Varnish sends regular
        # requests to.
        sub vcl_recv {
            headerproxy.call(req.backend_hint, "/webscript");
        }

        # Or send requests to a dedicated director just for your proxy script.
        sub vcl_recv {
            headerproxy.call(proxy_cluster.backend(), "/webscript");
        }

process
-------

Prototype
    ::

        headerproxy.process()

Context
    vcl_deliver

Returns
	VOID

Description
    Inserts the requested ``response`` headers.

Example
    ::

        sub vcl_deliver {
            headerproxy.process();
        }

error
-----

Prototype
    ::

        headerproxy.error()

Context
    vcl_recv

Returns
	STRING

Description
	Called after ``headerproxy.call()``, ``headerproxy.error()`` will return
	any error that might have occurred (as a string). Errors include CURL errors
	and JSON decoding errors. It will be empty if there were no errors.

Example
    ::

        sub vcl_recv {
            headerproxy.call();
            set req.http.X-VMOD-Error = headerproxy.error();
        }

INSTALLATION
============

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the varnishtest tool.

Usage::

    ./autogen.sh
    ./configure

If you have installed Varnish to a non-standard directory, call
``autogen.sh`` and ``configure`` with ``PKG_CONFIG_PATH`` pointing to
the appropriate path. For example, when varnishd configure was called
with ``--prefix=$PREFIX``, use

    PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
    export PKG_CONFIG_PATH

Make targets:

* make - builds the vmod
* make install - installs your vmod in `VMODDIR`
* make check - runs the unit tests in ``src/tests/*.vtc``

DEBUGGING
=========

Configure vmod for debugging with ``configure --enable-debug``. Useful debugging
data will be outputted to both the Varnish log and syslog.

LIMITATIONS
===========

* SSL responses from the web script url are currently not supported.

COMMON PROBLEMS
===============

* configure: error: Need varnish.m4

    Check if ``PKG_CONFIG_PATH`` has been set correctly before calling
    ``autogen.sh`` and ``configure``.

* No package 'libcurl' found

    Make sure ``libcurl-devel`` is installed.

