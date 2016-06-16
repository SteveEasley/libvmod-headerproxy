================
vmod_headerproxy
================

-------------------------
Varnish Header Proxy VMOD
-------------------------

:Author: Steve Easley
:Date: 2016-05-20
:Version: 0.1.0
:Manual section: 3

This vmod requires Varnish 4.1 or higher. While this vmod is in a development status just to let the dust settle, it is being used successfully in very high traffic website.

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

Varnish vmod that enables you to add custom HTTP headers to the client
request and response via whatever native programming language you prefer
(PHP, Node, Ruby, Python, etc).

This wizardry is made possible by the vmod passing a copy of every client
request to a web script (URL) you host. This should be a super fast script
which returns a json response containing the headers the vmod will add to the
request and response.

What can you do with this imbued power?

* Do additional processing on every client request that would be hard to do in
  VCL alone. Examples include logging, analytics, data warehousing, etc.
* Move complex business logic for controlling caching decisions out of VCL and
  into your web script. Examples include varying responses on geo location, AB
  testing, etc.
* Move complex VCL logic into your native application domain, where you and
  your developers can contribute more easily.
* Remove the need for numerous 3rd party vmods.

Check out the `Wiki <https://github.com/SteveEasley/libvmod-headerproxy/wiki>`_ for additional details.

Check out the `canonical example <example/>`_ for inspiration.

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
    Tells the vmod to send a copy of the client request to your web script,
    decodes its json response, then inserts any requested ``request`` headers
    into the client request.

Example
    ::

        # If your web script is hosted with your backend app, you can use the
        # same backend/director your app uses like this:
        sub vcl_recv {
            headerproxy.call(req.backend_hint, "/webscript");
        }

        # Or alternatively send requests to a dedicated director just for your
        # proxy script. This assumes you have already created proxy_cluster .
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
    Tells the vmod to inserts the requested ``response`` headers from your json.

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
data will be outputted to both the Varnish log.

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

