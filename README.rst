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

Note that this vmod requires Varnish 4.1 or higher.

SYNOPSIS
========
::

    import headerproxy;

    sub vcl_recv {
        headerproxy.call();
    }

    sub vcl_backend_response {
        headerproxy.process();
    }

    sub vcl_deliver {
        headerproxy.process();
    }

    sub vcl_synth {
        headerproxy.process();
    }

    sub vcl_pipe {
        headerproxy.process();
    }

    sub vcl_backend_error {
        headerproxy.process();
    }

**Its important to note** that ``headerproxy.process()`` is *absolutely*
required to be in all of the vcl methods shown in order to use this vmod. Read
on to understand why.

DESCRIPTION
===========

Varnish vmod that proxies every client request to a url running the scripting
language of your choice (PHP, Node, Python, etc). Your web script can return a
list of headers it wants the vmod to add to the client request and/or response.
This super cool ability enables you to:

* Do additional processing on every client request inside your web script.
  Examples include logging, data warehousing, etc.
* Move complex business logic for controlling caching decisions out of Varnish
  VCL and into your web script. Examples include varying responses on geo
  location, AB testing, etc.

Note that the web script you proxy requests to does not actually modify the
Varnish request or response directly. Instead your script will simply read the
request it receives, process it as needed, then output a json formatted list
of headers it wants the vmod to add to the client request and/or response.

Check out the canonical `example <example/>`_.

USAGE
=====

Usage involves:

1. Writing and hosting the web script you want to proxy requests to. See
   `WEB SCRIPT`_ for details.
2. Add the appropriate vmod functions to your vcl. See `VCL`_ for details.

WEB SCRIPT
==========

You must provide a web script url that the vmod can proxy requests to. This
script is something you write. It will be called with the client's request, and
must return a response containing any headers it wants added, if any.

Request from vmod
-----------------

The vmod will call your web script url with an http request containing a copy of
the same headers the client sent. The exception to this is the http ``method``
and ``url`` since those are used to direct the request to your web script. To
compensate the vmod gives you these additional headers:

X-Forwarded-Method
    Contains http method used by the client (``GET``, ``POST``, ``PUT``, etc).

X-Forwarded-Url
    Contains the ``url`` requested by the client.

X-Forwarded-For
    Contains client IP.

Response back to vmod
---------------------
Your web script must return a ``Content-Type: application/json`` response whose
body is a json object with the headers it wants the vmod to set in the various
Varnish methods. Here is an example response you might return::

    {
        'vcl_recv': {
            'X-Geo: us',
            'X-Device: tablet'
        },
        'vcl_backend_response': {
            'Vary: X-Geo, X-Device'
        },
        'vcl_deliver': {
            'Set-Cookie: geo=us',
            'Set-Cookie: device=tablet',
        }
    }

In this example you are requesting that inside ``vcl_recv`` the vmod add two
``request`` headers, inside ``vcl_backend_response`` add the ``Vary`` header to
the ``response``, and inside vcl_deliver adds two cookies to the ``response``.

The vmod only accepts 200 http response codes. If you return anything else the
response will be ignored.

Script example
--------------
::

    <?php
    if (false == isset($_COOKIE['geo'])) {
        $geo = your_geo_lookup($_SERVER['HTTP_X_FORWARDED_FOR']);
    }

    header('Content-type: application/json');

    echo json_encode(array(
        'vcl_recv' => array(
            "X-Geo: $geo",
        ),
        'vcl_backend_response' => array(
            "Vary: X-Geo",
        ),
        'vcl_deliver' => array(
            "Set-Cookie: geo=$geo"
        )
    ));

This example shows how you can vary a response on geo location. First we do
the geo lookup. Next, via the JSON output we request an ``X-Geo`` header be
added to the client request. Next we request a ``Vary`` header be added to the
response that comes from the backend (assuming the request was a cache miss).
The ``Vary`` header varies the cache lookup on the ``X-Geo`` header in the
request. Finally we request a ``Set-Cookie`` header be added to the response
going to the client. Setting a cookie allows us to bypass the potentially
expensive geo lookup at the top of the script.

Here is what a request that goes to the backend might look like after the
``headerproxy.call()`` call in ``vcl_recv``::

    GET /index.html HTTP/1.1
    Host: www.example.com
    X-Geo: us

And here is the response from the backend after the ``headerproxy.process()``
call in ``vcl_backend_response``::

    HTTP/1.1 200 OK
    Content-type: text/html
    Vary: X-Geo

And here is the response to the client after the ``headerproxy.process()`` call
in ``vcl_deliver``::

    HTTP/1.1 200 OK
    Content-type: text/html
    Set-Cookie: geo=us

VCL
===

The vmod determines the URL of your web script by querying for an available
backend and combining it's hostname/IP with an optional path you provide.
If neither the backend or path are configured in the vmod, then it will use
the backend chosen for normal requests, and the a path of "/". This feature
is very powerful in that it allows you to either use the same backends used
for the web script as you use for servicing requests, OR you can setup a
separate director and point the vmod to it.

To specify a specific backend or director you add the
``headerproxy.backend()`` call into ``vcl_recv`` (again this is optional). For
example if you have a round_robin director called ``cluster``, your would
point the vmod to it with ``headerproxy.backend(cluster.backend())``.

To specify a path to be appended to the url you add the ``headerproxy.path()``
call into ``vcl_recv``. For example, if might call
``headerproxy.path("/varnish.php")``.

You then add a ``headerproxy.call()`` call into ``vcl_recv`` (see `call`_). This
will do the following:

* Using curl the vmod sends the client request to the url of your web
  script. Your script will get an identical copy of all client request
  headers (see `Request from vmod`_).
* Your web script will return a list of headers that the vmod will add
  to the request (see `Response back to vmod`_).
* The vmod will insert the headers specified in a ``vcl_recv`` json key
  into the client ``request``. TIP: Headers you add here can be
  referenced by a ``Vary`` response header, which is where the real
  power comes in.

Finally you add a ``headerproxy.process()`` call into each of
``vcl_backend_response``,  ``vcl_deliver``, ``vlc_synth``, ``vcl_pipe``, and
``vcl_backend_error`` (see `process`_). The vmod will do the following actions
dependent on which Varnish method ``headerproxy.process()`` is invoked from:

vcl_backend_response
    * The vmod will insert the headers requested in a
      ``vcl_backend_response`` json key into the backend ``response``. TIP:
      Headers here will be cached along with the response by Varnish.
      This is where you will likely add a ``Vary`` header. It can be
      combined with any ``Vary`` headers sent by your backend with the vcl
      command ``std.collect(beresp.http.Vary)``;

vcl_deliver
    * The vmod will insert the headers requested in a ``vcl_deliver`` json
      key into the client ``response``. TIP: Headers set here wont be
      cached. Its the ideal place to insert ``Set-Cookie`` headers.
    * The vmod will release allocated resources. **NOTE**: It is *absolutely
      imperative* you call ``headerproxy.process()`` here (see `process`_).

vcl_synth
    * The vmod will release allocated resources. **NOTE**: It is *absolutely
      imperative* you call ``headerproxy.process()`` here (see `process`_).

vcl_pipe
    * The vmod will release allocated resources. **NOTE**: It is *absolutely
      imperative* you call ``headerproxy.process()`` here (see `process`_).

vcl_backend_error
    * The vmod will release allocated resources. **NOTE**: It is *absolutely
      imperative* you call ``headerproxy.process()`` here (see `process`_).

FUNCTIONS
=========

backend
-------

Prototype
    ::

        headerproxy.url(BACKEND)

Context
    vcl_recv

Returns
	VOID

Description
	Sets the varnish backend or director to proxy requests to.

Example
    ::

        backend b1 { .host "10.1.1.1"; }

        sub vcl_init {
            new cluster = directors.round_robin();
            cluster.add_backend(b1);
        }

        sub vcl_recv {
            headerproxy.backend(b1);
            # OR
            headerproxy.backend(cluster.backend());
        }

path
----

Prototype
    ::

        headerproxy.path(STRING)

Context
    vcl_recv

Returns
    VOID

Description
    Sets the url path of your web script.

Example
    ::

        sub vcl_recv {
            headerproxy.path("/webscript.php");
        }

call
----

Prototype
    ::

        headerproxy.call()

Context
    vcl_recv

Returns
	VOID

Description
	Tells the vmod to proxy the client request to your web script then inserts
	the	requested ``request`` headers from your json response. Based on your vcl
	logic you can opt to not proxy the request by simply not calling
	``headerproxy.call``, but note that you **must** still call
	``headerproxy.process``	in ``vcl_deliver``, ``vcl_synth``, ``vcl_pipe`` and
	``vcl_backend_error`` as noted below).

Example
    ::

        sub vcl_recv {
            headerproxy.call();
        }

process
-------

Prototype
    ::

        headerproxy.process()

Context
    vcl_backend_response, vcl_deliver, vcl_synth, vcl_pipe, vcl_backend_error

Returns
	VOID

Description
	Called in ``vcl_backend_response`` the vmod inserts the requested
	``response`` headers.

	Called in ``vcl_deliver`` the vmod inserts the requested ``response``
	headers, then finalizes the request to free up vmod allocated resources.

	Called in ``vcl_synth``, ``vcl_pipe``, ``vcl_backend_error`` the vmod
	finalizes the request to free up vmod allocated resources.

	**NOTE**: It is *absolutely imperative* you call ``headerproxy.process()``
	in all four of ``vcl_deliver``, ``vcl_synth``, ``vcl_pipe`` and
	``vcl_backend_error`` because these are the three exit points of a varnish
	response to a client. This isthe only way the vmod can know the
	request/response is complete. Failing to do so will quickly cause the vmod
	to run out of allocated memory. It is safe to call ``headerproxy.process()``
	in all four even if your vcl logic chose not to call
	``headerproxy.process()``	in ``vcl_recv``.

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
	Called after ``headerproxy.process()``, ``headerproxy.error()`` will return
	any error that might have occurred (as a string). Errors include CURL errors
	and JSON decoding errors. It will be empty if there were no errors.

Example
    ::

        sub vcl_recv {
            headerproxy.process();
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

