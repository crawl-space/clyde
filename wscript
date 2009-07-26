#!/usr/bin/python

VERSION = "0.0.1"
APPNAME = "clyde"

def configure(conf):
    conf.check_tool("gcc")

    conf.check_cfg(package="glib-2.0", uselib_store="GLIB",
            args="--cflags --libs", mandatory=True)
    conf.check_cfg(package="gtk+-2.0", uselib_store="GTK",
            args="--cflags --libs", mandatory=True)

    conf.define("VERSION", VERSION)

    conf.write_config_header()

def build(bld):
    applet = bld.new_task_gen(
            features = "cc cprogram",
            includes = "# src/applet",
            uselib = "GLIB",
            target = "clyde"
    )

    applet.find_sources_in_dirs("src/applet")
