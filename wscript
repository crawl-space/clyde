#!/usr/bin/python

VERSION = "0.0.1"
APPNAME = "clyde"

def configure(conf):
    conf.check_tool("gcc")

    conf.check_cfg(package="glib-2.0", mandatory=True)
    conf.check_cfg(package="gtk+-2.0", mandatory=True)

    conf.define("VERSION", VERSION)

def build(bld):
    obj = bld.new_task_gen(
            features = "cc cprogram",
            includes = "# src/applet",
            uselib = "GTK",
            target = "clyde"
    )

    obj.find_sources_in_dirs("src/applet")
