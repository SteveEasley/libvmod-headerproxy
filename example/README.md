# Canonical vmod_headerproxy example

This directory contains a working example of how to use vmod_headerproxy. The
included files are:

* default.vcl. You can start varnish with this file with something like:

         varnishd -d -f libvmod-headerproxy/example/default.vcl -a 0.0.0.0:80

* headerproxy.php. An example script to proxy requests to. If you have PHP
  5.4 you can start a quick web server with something like:

         php -S localhost:8000 -t libvmod-headerproxy/example/

* index.php. And example backend service to send requests to. You can serve
  this from the same server as headerproxy.php
